use crate::uefi::{EfiGuid, EfiStatus, SystemTable};
use core::ffi::c_void;

const EFI_ERROR_BIT: usize = 1usize << (usize::BITS - 1);
const EFI_INVALID_PARAMETER: EfiStatus = EFI_ERROR_BIT | 2;
const EFI_UNSUPPORTED: EfiStatus = EFI_ERROR_BIT | 3;
const EFI_DEVICE_ERROR: EfiStatus = EFI_ERROR_BIT | 7;
const EFI_NOT_FOUND: EfiStatus = EFI_ERROR_BIT | 14;

const EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID: EfiGuid = EfiGuid {
    data1: 0x9042_a9de,
    data2: 0x23dc,
    data3: 0x4a38,
    data4: [0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a],
};

const GOP_PIXEL_RED_GREEN_BLUE_RESERVED_8BIT_PER_COLOR: u32 = 0;
const GOP_PIXEL_BLUE_GREEN_RED_RESERVED_8BIT_PER_COLOR: u32 = 1;

/// The pixel ordering used by the firmware's GOP framebuffer.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PixelFormat {
    RgbReserved8,
    BgrReserved8,
}

/// The active GOP framebuffer and its dimensions.
#[derive(Clone, Copy, Debug)]
pub struct FramebufferInfo {
    pub base: *mut u8,
    pub byte_len: usize,
    pub width: u32,
    pub height: u32,
    pub pixels_per_scan_line: u32,
    pub pixel_format: PixelFormat,
}

/// A GOP initialization error with a firmware status and a stable diagnostic.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GopError {
    status: EfiStatus,
    message: &'static str,
}

impl GopError {
    const fn new(status: EfiStatus, message: &'static str) -> Self {
        Self { status, message }
    }

    /// Returns the EFI status associated with the failure.
    pub const fn status(&self) -> EfiStatus {
        self.status
    }

    /// Returns a static diagnostic suitable for reporting by the caller.
    pub const fn message(&self) -> &'static str {
        self.message
    }
}

#[repr(C)]
struct GopPixelBitmask {
    red_mask: u32,
    green_mask: u32,
    blue_mask: u32,
    reserved_mask: u32,
}

#[repr(C)]
union GopPixelInformation {
    bitmask: GopPixelBitmask,
    reserved: [u32; 4],
}

#[repr(C)]
struct GopModeInfo {
    version: u32,
    horizontal_resolution: u32,
    vertical_resolution: u32,
    pixel_format: u32,
    pixel_information: GopPixelInformation,
    pixels_per_scan_line: u32,
}

#[repr(C)]
struct GopMode {
    max_mode: u32,
    mode: u32,
    info: *mut GopModeInfo,
    size_of_info: usize,
    frame_buffer_base: u64,
    frame_buffer_size: usize,
}

#[repr(C)]
struct GopBltPixel {
    blue: u8,
    green: u8,
    red: u8,
    reserved: u8,
}

#[repr(C)]
struct GopProtocol {
    query_mode: Option<
        unsafe extern "efiapi" fn(
            this: *mut GopProtocol,
            mode_number: u32,
            size_of_info: *mut usize,
            info: *mut *mut GopModeInfo,
        ) -> EfiStatus,
    >,
    set_mode:
        Option<unsafe extern "efiapi" fn(this: *mut GopProtocol, mode_number: u32) -> EfiStatus>,
    blt: Option<
        unsafe extern "efiapi" fn(
            this: *mut GopProtocol,
            blt_buffer: *mut GopBltPixel,
            blt_operation: u32,
            source_x: usize,
            source_y: usize,
            destination_x: usize,
            destination_y: usize,
            width: usize,
            height: usize,
            delta: usize,
        ) -> EfiStatus,
    >,
    mode: *mut GopMode,
}

/// Opens the system GOP protocol and describes its currently active framebuffer.
///
/// # Safety
/// `system_table` and the firmware pointers reachable through it must be valid
/// for the duration of the call, and firmware services must remain callable.
pub unsafe fn open(system_table: &SystemTable) -> Result<FramebufferInfo, GopError> {
    let boot_services = system_table.boot_services;
    if boot_services.is_null() {
        return Err(GopError::new(
            EFI_INVALID_PARAMETER,
            "UEFI boot services table is null",
        ));
    }

    let locate_protocol = unsafe { (*boot_services).locate_protocol }.ok_or_else(|| {
        GopError::new(
            EFI_UNSUPPORTED,
            "UEFI boot services do not provide LocateProtocol",
        )
    })?;

    let mut protocol_pointer: *mut c_void = core::ptr::null_mut();
    let status = unsafe {
        locate_protocol(
            &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
            core::ptr::null_mut(),
            &mut protocol_pointer,
        )
    };
    if status & EFI_ERROR_BIT != 0 {
        return Err(GopError::new(
            status,
            "LocateProtocol could not open the Graphics Output Protocol",
        ));
    }
    if protocol_pointer.is_null() {
        return Err(GopError::new(
            EFI_NOT_FOUND,
            "LocateProtocol returned a null Graphics Output Protocol",
        ));
    }

    let protocol = protocol_pointer.cast::<GopProtocol>();
    let mode = unsafe { (*protocol).mode };
    if mode.is_null() {
        return Err(GopError::new(
            EFI_DEVICE_ERROR,
            "Graphics Output Protocol has no active mode",
        ));
    }

    let (info, size_of_info, base, byte_len) = unsafe {
        let mode = &*mode;
        (
            mode.info,
            mode.size_of_info,
            mode.frame_buffer_base,
            mode.frame_buffer_size,
        )
    };
    if info.is_null()
        || size_of_info < core::mem::size_of::<GopModeInfo>()
        || base == 0
        || byte_len == 0
    {
        return Err(GopError::new(
            EFI_DEVICE_ERROR,
            "Graphics Output Protocol returned incomplete framebuffer data",
        ));
    }

    let (width, height, pixels_per_scan_line, pixel_format) = unsafe {
        let info = &*info;
        let pixel_format = match info.pixel_format {
            GOP_PIXEL_RED_GREEN_BLUE_RESERVED_8BIT_PER_COLOR => PixelFormat::RgbReserved8,
            GOP_PIXEL_BLUE_GREEN_RED_RESERVED_8BIT_PER_COLOR => PixelFormat::BgrReserved8,
            _ => {
                return Err(GopError::new(
                    EFI_UNSUPPORTED,
                    "Graphics Output Protocol uses an unsupported pixel format",
                ));
            }
        };
        (
            info.horizontal_resolution,
            info.vertical_resolution,
            info.pixels_per_scan_line,
            pixel_format,
        )
    };

    if width == 0 || height == 0 || pixels_per_scan_line < width {
        return Err(GopError::new(
            EFI_DEVICE_ERROR,
            "Graphics Output Protocol returned invalid framebuffer dimensions",
        ));
    }

    let required_bytes = (pixels_per_scan_line as usize)
        .checked_mul(height as usize)
        .and_then(|pixels| pixels.checked_mul(4))
        .ok_or_else(|| {
            GopError::new(
                EFI_DEVICE_ERROR,
                "Graphics Output Protocol framebuffer dimensions overflow",
            )
        })?;
    if required_bytes > byte_len {
        return Err(GopError::new(
            EFI_DEVICE_ERROR,
            "Graphics Output Protocol framebuffer is smaller than its mode",
        ));
    }

    Ok(FramebufferInfo {
        base: base as usize as *mut u8,
        byte_len,
        width,
        height,
        pixels_per_scan_line,
        pixel_format,
    })
}
