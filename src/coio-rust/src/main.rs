#![no_std]
#![no_main]
#![forbid(unsafe_op_in_unsafe_fn)]

mod gop;
mod graphics;
mod uefi;

use core::ffi::c_void;
use core::panic::PanicInfo;
use core::ptr;
use core::sync::atomic::{AtomicPtr, Ordering};
use uefi::{EfiStatus, SystemTable, EFI_ABORTED, EFI_INVALID_PARAMETER, EFI_SUCCESS};

static SYSTEM_TABLE_FOR_PANIC: AtomicPtr<SystemTable> = AtomicPtr::new(ptr::null_mut());

#[no_mangle]
pub unsafe extern "efiapi" fn efi_main(
    _image_handle: *mut c_void,
    system_table: *mut SystemTable,
) -> EfiStatus {
    if system_table.is_null() {
        return EFI_INVALID_PARAMETER;
    }
    SYSTEM_TABLE_FOR_PANIC.store(system_table, Ordering::Relaxed);

    // SAFETY: UEFI calls this entrypoint with a valid system table pointer.
    let system_table = unsafe { &*system_table };
    // SAFETY: the system table and firmware service pointers are valid on entry.
    let framebuffer = match unsafe { gop::open(system_table) } {
        Ok(framebuffer) => framebuffer,
        Err(error) => {
            // SAFETY: the system table was supplied by the firmware for this call.
            let _ = unsafe { uefi::report_error(system_table, error.message(), error.status()) };
            return error.status();
        }
    };

    // SAFETY: `gop::open` returned the active firmware framebuffer range.
    let render_result = unsafe { graphics::render_gop_ready(&framebuffer) };
    if let Err(error) = render_result {
        // SAFETY: the system table was supplied by the firmware for this call.
        let _ = unsafe { uefi::report_error(system_table, error.message(), error.status()) };
        return error.status();
    }

    EFI_SUCCESS
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    let system_table = SYSTEM_TABLE_FOR_PANIC.load(Ordering::Relaxed);
    if !system_table.is_null() {
        // SAFETY: this pointer was stored from the valid UEFI entrypoint table.
        let system_table = unsafe { &*system_table };
        // SAFETY: panic occurs while the UEFI app and its firmware services are active.
        let _ = unsafe { uefi::report_error(system_table, "panic; execution halted", EFI_ABORTED) };
    }

    loop {
        core::hint::spin_loop();
    }
}
