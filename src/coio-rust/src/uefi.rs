use core::ffi::c_void;

pub type EfiStatus = usize;

pub const EFI_SUCCESS: EfiStatus = 0;
const EFI_ERROR_BIT: EfiStatus = 1usize << (usize::BITS - 1);
pub const EFI_INVALID_PARAMETER: EfiStatus = EFI_ERROR_BIT | 2;
pub const EFI_DEVICE_ERROR: EfiStatus = EFI_ERROR_BIT | 7;
pub const EFI_ABORTED: EfiStatus = EFI_ERROR_BIT | 21;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct EfiGuid {
    pub data1: u32,
    pub data2: u16,
    pub data3: u16,
    pub data4: [u8; 8],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct EfiTableHeader {
    pub signature: u64,
    pub revision: u32,
    pub header_size: u32,
    pub crc32: u32,
    pub reserved: u32,
}

pub type EfiOutputString =
    unsafe extern "efiapi" fn(this: *mut SimpleTextOutputProtocol, string: *const u16) -> EfiStatus;

#[repr(C)]
pub struct SimpleTextOutputProtocol {
    pub reset: Option<
        unsafe extern "efiapi" fn(
            this: *mut SimpleTextOutputProtocol,
            extended_verification: u8,
        ) -> EfiStatus,
    >,
    pub output_string: Option<EfiOutputString>,
}

pub type EfiLocateProtocol = unsafe extern "efiapi" fn(
    protocol: *const EfiGuid,
    registration: *mut c_void,
    interface: *mut *mut c_void,
) -> EfiStatus;

/// The boot-services prefix through LocateProtocol.
///
/// The 37 pointer-sized entries include the reserved slot in the UEFI table.
/// Keeping them opaque preserves the firmware-defined offset without duplicating
/// unused function signatures.
#[repr(C)]
pub struct BootServices {
    pub header: EfiTableHeader,
    _reserved_before_locate_protocol: [usize; 37],
    pub locate_protocol: Option<EfiLocateProtocol>,
}

/// UEFI system table prefix through ConfigurationTable, with the fields used by
/// the COIO Rust application exposed to its protocol and diagnostic modules.
#[repr(C)]
pub struct SystemTable {
    pub header: EfiTableHeader,
    pub firmware_vendor: *const u16,
    pub firmware_revision: u32,
    pub console_in_handle: *mut c_void,
    pub console_in: *mut c_void,
    pub console_out_handle: *mut c_void,
    pub console_out: *mut SimpleTextOutputProtocol,
    pub standard_error_handle: *mut c_void,
    pub standard_error: *mut SimpleTextOutputProtocol,
    pub runtime_services: *mut c_void,
    pub boot_services: *mut BootServices,
    pub number_of_table_entries: usize,
    pub configuration_table: *mut c_void,
}

// These assertions protect the raw AArch64 UEFI offsets used by the GOP
// module from accidental field-order or ABI changes.
const _: [(); 24] = [(); core::mem::size_of::<EfiTableHeader>()];
const _: [(); 16] = [(); core::mem::size_of::<EfiGuid>()];
const _: [(); 96] = [(); core::mem::offset_of!(SystemTable, boot_services)];
const _: [(); 24 + 37 * core::mem::size_of::<usize>()] =
    [(); core::mem::offset_of!(BootServices, locate_protocol)];

const DIAGNOSTIC_CAPACITY: usize = 384;
const HEX_DIGITS: &[u8; 16] = b"0123456789ABCDEF";

/// Writes an allocation-free ASCII diagnostic and its EFI status to the UEFI
/// text console (falling back to StandardError). Returns the console status.
///
/// # Safety
/// `system_table` and any console protocol pointers it contains must be valid
/// firmware tables for the duration of the call.
pub unsafe fn report_error(
    system_table: &SystemTable,
    message: &str,
    status: EfiStatus,
) -> EfiStatus {
    let mut buffer = [0_u16; DIAGNOSTIC_CAPACITY];
    let mut cursor = 0;
    push_ascii(&mut buffer, &mut cursor, "COIO Rust error: ");
    push_ascii(&mut buffer, &mut cursor, message);
    push_ascii(&mut buffer, &mut cursor, " (EFI status 0x");

    let hex_width = (usize::BITS / 4) as usize;
    for digit in (0..hex_width).rev() {
        let nibble = ((status >> (digit * 4)) & 0x0f) as usize;
        push_code_unit(&mut buffer, &mut cursor, HEX_DIGITS[nibble] as u16);
    }
    push_ascii(&mut buffer, &mut cursor, ")\r\n");
    buffer[cursor] = 0;

    // SAFETY: the caller guarantees the system table and console pointers are
    // valid, and `buffer` remains alive and NUL-terminated for the call.
    unsafe { output_with_fallback(system_table, buffer.as_ptr()) }
}

fn push_ascii(buffer: &mut [u16], cursor: &mut usize, text: &str) {
    for byte in text.bytes() {
        if *cursor + 1 >= buffer.len() {
            break;
        }
        let code_unit = if byte.is_ascii() {
            byte as u16
        } else {
            b'?' as u16
        };
        push_code_unit(buffer, cursor, code_unit);
    }
}

fn push_code_unit(buffer: &mut [u16], cursor: &mut usize, code_unit: u16) {
    if *cursor + 1 < buffer.len() {
        buffer[*cursor] = code_unit;
        *cursor += 1;
    }
}

unsafe fn output_with_fallback(system_table: &SystemTable, string: *const u16) -> EfiStatus {
    // SAFETY: inherited from this function's caller contract.
    let primary = unsafe { output_to(system_table.console_out, string) };
    if let Some(status) = primary {
        if status == EFI_SUCCESS {
            return status;
        }
    }

    if !system_table.standard_error.is_null()
        && system_table.standard_error != system_table.console_out
    {
        // SAFETY: inherited from this function's caller contract.
        if let Some(status) = unsafe { output_to(system_table.standard_error, string) } {
            if status == EFI_SUCCESS {
                return status;
            }
            return primary.unwrap_or(status);
        }
    }

    primary.unwrap_or(EFI_INVALID_PARAMETER)
}

unsafe fn output_to(
    protocol: *mut SimpleTextOutputProtocol,
    string: *const u16,
) -> Option<EfiStatus> {
    if protocol.is_null() {
        return None;
    }

    // SAFETY: the caller guarantees the non-null pointer is a live firmware
    // text-output protocol for the duration of this call.
    let output_string = unsafe { (*protocol).output_string }?;
    // SAFETY: the protocol pointer is live and `string` points to the caller's
    // NUL-terminated UTF-16 buffer, which remains in scope through the call.
    Some(unsafe { output_string(protocol, string) })
}
