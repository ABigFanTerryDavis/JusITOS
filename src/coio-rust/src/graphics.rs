use crate::gop::{FramebufferInfo, PixelFormat};
use crate::uefi::{EfiStatus, EFI_DEVICE_ERROR, EFI_INVALID_PARAMETER};

#[derive(Clone, Copy, Debug)]
pub struct GraphicsError {
    status: EfiStatus,
    message: &'static str,
}

impl GraphicsError {
    pub const fn status(&self) -> EfiStatus {
        self.status
    }

    pub const fn message(&self) -> &'static str {
        self.message
    }
}

/// Draws a compact, allocation-free status screen after GOP initialization.
///
/// # Safety
/// `framebuffer.base` must point to writable, mapped memory for at least
/// `framebuffer.byte_len` bytes for the duration of the call.
pub unsafe fn render_gop_ready(framebuffer: &FramebufferInfo) -> Result<(), GraphicsError> {
    validate(framebuffer)?;

    let background = Color::new(8, 18, 38);
    let header = Color::new(28, 63, 108);
    let bright = Color::new(226, 239, 255);
    let ready = Color::new(95, 232, 150);

    // SAFETY: the caller guarantees that the framebuffer range is mapped and
    // writable; `validate` checks the dimensions against its advertised length.
    unsafe {
        fill_rect(
            framebuffer,
            0,
            0,
            framebuffer.width,
            framebuffer.height,
            background,
        );
        let header_height = core::cmp::max(1, framebuffer.height / 5);
        fill_rect(framebuffer, 0, 0, framebuffer.width, header_height, header);
        draw_text(framebuffer, 12, 10, "COIO RUST", bright);
        draw_text(
            framebuffer,
            12,
            header_height.saturating_add(14),
            "GOP READY",
            ready,
        );

        let bar_height = core::cmp::max(2, framebuffer.height / 24);
        let bar_y = framebuffer
            .height
            .saturating_sub(bar_height.saturating_add(12));
        let margin = core::cmp::min(framebuffer.width / 12, framebuffer.width / 2);
        let bar_width = framebuffer.width.saturating_sub(margin.saturating_mul(2));
        fill_rect(framebuffer, margin, bar_y, bar_width, bar_height, ready);
    }

    Ok(())
}

#[derive(Clone, Copy)]
struct Color(u8, u8, u8);

impl Color {
    const fn new(red: u8, green: u8, blue: u8) -> Self {
        Self(red, green, blue)
    }
}

fn validate(framebuffer: &FramebufferInfo) -> Result<(), GraphicsError> {
    if framebuffer.base.is_null()
        || framebuffer.width == 0
        || framebuffer.height == 0
        || framebuffer.pixels_per_scan_line < framebuffer.width
    {
        return Err(GraphicsError {
            status: EFI_INVALID_PARAMETER,
            message: "framebuffer dimensions or base address are invalid",
        });
    }

    let required_bytes = (framebuffer.pixels_per_scan_line as usize)
        .checked_mul(framebuffer.height as usize)
        .and_then(|pixels| pixels.checked_mul(4))
        .ok_or(GraphicsError {
            status: EFI_DEVICE_ERROR,
            message: "framebuffer dimensions overflow addressable memory",
        })?;
    if required_bytes > framebuffer.byte_len {
        return Err(GraphicsError {
            status: EFI_DEVICE_ERROR,
            message: "framebuffer storage is smaller than its dimensions",
        });
    }

    Ok(())
}

unsafe fn fill_rect(
    framebuffer: &FramebufferInfo,
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    color: Color,
) {
    let x_end = x.saturating_add(width).min(framebuffer.width);
    let y_end = y.saturating_add(height).min(framebuffer.height);
    let x_start = x.min(framebuffer.width);
    let y_start = y.min(framebuffer.height);

    for row in y_start..y_end {
        for column in x_start..x_end {
            // SAFETY: this helper inherits the framebuffer validity precondition.
            unsafe { write_pixel(framebuffer, column, row, color) };
        }
    }
}

unsafe fn write_pixel(framebuffer: &FramebufferInfo, x: u32, y: u32, color: Color) {
    let pixel_index = (y as usize) * (framebuffer.pixels_per_scan_line as usize) + (x as usize);
    let byte_offset = pixel_index * 4;
    let (first, second, third) = match framebuffer.pixel_format {
        PixelFormat::RgbReserved8 => (color.0, color.1, color.2),
        PixelFormat::BgrReserved8 => (color.2, color.1, color.0),
    };

    unsafe {
        let pixel = framebuffer.base.wrapping_add(byte_offset);
        core::ptr::write_volatile(pixel, first);
        core::ptr::write_volatile(pixel.wrapping_add(1), second);
        core::ptr::write_volatile(pixel.wrapping_add(2), third);
        core::ptr::write_volatile(pixel.wrapping_add(3), 0);
    }
}

unsafe fn draw_text(framebuffer: &FramebufferInfo, x: u32, y: u32, text: &str, color: Color) {
    let mut cursor_x = x;
    for character in text.bytes() {
        let glyph = glyph(character);
        for (row, bits) in glyph.iter().enumerate() {
            for column in 0..5 {
                if bits & (1_u8 << (4 - column as u32)) != 0 {
                    // SAFETY: text rendering uses the same validated framebuffer.
                    unsafe {
                        fill_rect(
                            framebuffer,
                            cursor_x.saturating_add(column as u32),
                            y.saturating_add(row as u32),
                            1,
                            1,
                            color,
                        )
                    };
                }
            }
        }
        cursor_x = cursor_x.saturating_add(6);
    }
}

fn glyph(character: u8) -> [u8; 7] {
    match character {
        b'A' => [0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11],
        b'C' => [0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e],
        b'D' => [0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e],
        b'E' => [0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f],
        b'G' => [0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f],
        b'I' => [0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f],
        b'O' => [0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e],
        b'P' => [0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10],
        b'R' => [0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11],
        b'S' => [0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e],
        b'T' => [0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04],
        b'U' => [0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e],
        b'Y' => [0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04],
        _ => [0; 7],
    }
}
