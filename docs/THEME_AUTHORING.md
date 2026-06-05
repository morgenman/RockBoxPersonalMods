# Theme Authoring Guide — Album Art Thumbnail Extensions

This document covers the custom extensions added to Rockbox for album art thumbnails in the database browser view.

---

## Thumbnail Row Height

The database browser displays album art thumbnails on the left of each list row. The thumbnail size is derived from the row height:

```
thumbnail_size = min(row_height - 4, 44)
```

The row height is configurable via **Settings → Theme → Album Art Row Height**. Values range from the theme's default line height up to the maximum of 48 pixels.

---

## Artwork Padding

**Setting:** Settings → Theme → Album Art Padding  
**Range:** 0–6 px per side  
**Default:** 0  
**Config key:** `album art padding`

When padding is set to `n`, the artwork is inset by `n` pixels on every side within its container. The container's dimensions are `thumbnail_size + 2n` wide and tall. To fit the container without clipping, the list row height is automatically increased by `2n` pixels beyond the configured row height.

The artwork thumbnail size itself is **not** reduced by padding — it is still `min(row_height - 4, 44)`. Only the spacing around it grows.

### Sizing summary

| Row Height | Padding | Container | Effective Row Height |
|---|---|---|---|
| 32 | 0 | 28×28 | 32 |
| 32 | 2 | 28+4 = 32×32 | 36 |
| 32 | 4 | 28+8 = 36×36 | 40 |
| 44 | 3 | 40+6 = 46×46 | 50 |

### Setting it from a theme .cfg file

```
album art row height: 32
album art padding: 2
```

Both settings are theme-aware (`F_THEMESETTING`) and are saved and restored when a theme .cfg is loaded.

---

## Fallback Thumbnail Image

When an album has no embedded art and no folder image, the database browser normally shows the item's file-type icon centered in the art area. You can replace this with a custom image by providing a **fallback thumbnail**.

### Configuring the Fallback

Add this to your theme's `.cfg` file:

```
album art fallback: /.rockbox/themes/mytheme_fallback.bmp
```

**Config key:** `album art fallback`  
**Value:** Full path to a BMP file on the device, or empty to disable  
**Default:** empty (built-in file-type icon shown)

Each theme specifies its own fallback image — different themes can use different images. The path is saved and restored as part of the theme's settings.

By convention, keep theme BMP assets in `/.rockbox/themes/` alongside your `.cfg` file so that the full path is predictable.

### Image Format

- **Format**: BMP (Windows Bitmap), 24-bit or 16-bit RGB
- **Recommended resolution**: at least 44×44 pixels
- **Aspect ratio**: square (1:1)

Rockbox scales the image to the current thumbnail size automatically, so you only need to provide one file. For best results, use a high-resolution source image (44×44 or larger) with clean edges that scale well at small sizes.

### Sizes Defined in the Menu

The **Thumbnail Row Height** setting controls the rendered size:

| Row Height Setting | Thumbnail Size |
|---|---|
| 16 px | 12 px |
| 20 px | 16 px |
| 24 px | 20 px |
| 28 px | 24 px |
| 32 px | 28 px |
| 36 px | 32 px |
| 40 px | 36 px |
| 44 px | 40 px |
| 48 px | 44 px |

### When the Fallback is Cached

A scaled `.bin` for the current thumbnail size is generated in
`/.rockbox/thumbcache/` during the database update pass — the same operation that
builds album thumbnails. Browse-time loading reads the `.bin` directly (no BMP
decode overhead).

If the source BMP exists but the `.bin` is absent (e.g. the file was placed after
the last database update), the firmware decodes and caches it on the first browse
open. Missing source BMPs are silently ignored.

### Removing the Fallback

Set `album art fallback:` to empty or remove the line from your `.cfg`. The browser
will return to showing the item's file-type icon for albums with no art.

---

## Layout

In the database browser, every row reserves space for the art area regardless of whether art is available:

```
Pointer mode:  [>] [art or icon] [text...]
Bar mode:      [art or icon] [text...]
```

The text column is always aligned — items with art, items with the fallback image, and items with a centered icon all start their text at the same horizontal position.

---

## Rebuilding the Thumbnail Cache

Thumbnails are pre-built and stored in `/.rockbox/thumbcache/`. To regenerate them (e.g. after adding new albums or changing the fallback image):

1. Go to **Settings → General Settings → Database**
2. Select **Update Now** or **Force Update**

The thumbnail cache is rebuilt in the background after the database update completes.

To clear all cached thumbnails and start fresh, use **Force Update** — this deletes the existing cache before rebuilding.
