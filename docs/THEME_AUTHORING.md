# Theme Authoring Guide — RockboxMod

This build of Rockbox includes firmware-level extensions for album art thumbnails
and list appearance. Themes can opt into these features by adding settings to their
`.cfg` file. No SBS/WPS changes are required for basic support.

---

## List Corner Radius

Draws smooth rounded corners on every list item using a circular-arc mask.  
The firmware handles all drawing — no bitmap assets needed.

**Config key:** `list corner radius`  
**Values:** `0` (off, default), `2`, `4`, `6`, `8`, `10`, `12` (pixels)  
**User override:** Theme Settings → List Corner Radius

```cfg
# Recommended value for a modern rounded look:
list corner radius: 6
```

The arc shape is computed from the circle equation, so all radii produce a smooth
quarter-circle curve — not a blocky square cut. Larger values give more pronounced
rounding.

| Value | Shape |
|-------|-------|
| 0     | Square corners (default if not set by theme) |
| 2     | Very subtle rounding |
| 4     | Gentle curve |
| 6     | Moderate rounding (adwaitapod default) |
| 8     | Noticeable curve |
| 10    | Strong rounding |
| 12    | Maximum supported radius |

Corners are drawn on the main screen only and adapt to any row height automatically.
The fill colour is always the list viewport background, so corners blend correctly
with the theme background.

> **Note:** If the user changes this setting in the menu, their choice persists until
> they reload the theme (which reapplies the `.cfg` value).

---

## Album Art Size (Database View Only)

Sets the thumbnail pixel size in the database browser (ID3 tag view). The row
height is automatically `art size + 4px` to leave a small margin around the image.
Has no effect in the file browser or any other screen.

**Config key:** `album art size`  
**Values:** `auto` (derive from font height, default), `16`, `20`, `24`, `28`, `32`,
`36`, `40`, `44`, `64`, `96`, `128` (the **art/thumbnail** size in pixels — row height
is this value plus 4)  
**User override:** Theme Settings → Album Art Size

```cfg
# 44px art thumbnails → row height is 48px automatically:
album art size: 44
```

At `auto`, the thumbnail size is derived from the theme's default line height minus 4px.

Album art is only shown in the **database view** (Artists → Albums → Tracks, etc.),
never in the file browser. One thumbnail per album per size is cached to disk in
`/.rockbox/thumbcache/Np/` (e.g. `44p/` for 44px thumbnails). The cache is built
from **Debug → Album Art Thumbnails** — it is no longer built automatically on
database update.

| Art size | Row height | Notes |
|---|---|---|
| 16 | 20 | Compact |
| 20 | 24 | |
| 24 | 28 | |
| 28 | 32 | |
| 32 | 36 | |
| 36 | 40 | |
| 40 | 44 | |
| 44 | 48 | Maximum for most icon-size themes |
| 64 | 68 | Large thumbnail look |
| 96 | 100 | |
| 128 | 132 | Maximum supported size |

### Theme-preferred auto size

If the user has "Album Art Size" set to Auto, a theme can suggest a specific size
that takes priority over the font-derived default:

**Config key:** `album art theme size`  
**Value:** Art size in pixels, or `0` (unset, default)

```cfg
# Suggest 44px art when the user hasn't overridden the size:
album art theme size: 44
```

This key is theme-controlled (`F_THEMESETTING`) and is never shown in any user menu.
It has no effect if the user has chosen an explicit size from the "Album Art Size"
menu.

---

## Album Art Padding (Database View Only)

Adds inset space around the artwork inside its column, creating a visual container
effect without requiring any bitmap assets.

**Config key:** `album art padding`  
**Values:** `0`–`6` (pixels per side, default `0`)  
**User override:** Theme Settings → Album Art Padding

```cfg
album art padding: 2
```

When set to `n`, the artwork is inset `n` pixels from each edge of its slot. The
row height grows by `2n` automatically to keep the artwork at full size. The
thumbnail itself is **not** shrunk — only the surrounding space increases.

| Art Size | Padding | Effective Row Height |
|---|---|---|
| 28 | 0 | 32 |
| 28 | 2 | 36 |
| 28 | 4 | 40 |
| 44 | 3 | 54 |

---

## Fallback Thumbnail Image (Database View Only)

When an album has no embedded art and no folder image, the database browser shows
the item's file-type icon by default. You can replace this with a custom image that
matches your theme's style.

### Configuring the fallback image

Add this to your theme's `.cfg` file, pointing to the BMP you want to use:

```cfg
album art fallback: /.rockbox/themes/mytheme_fallback.bmp
```

The path can point to any BMP on the device. By convention, keep your theme's assets
in `/.rockbox/themes/` alongside your `.cfg` file.

**Config key:** `album art fallback`  
**Value:** Full path to a BMP file, or empty (no fallback, shows icon instead)  
**Default:** empty (built-in file-type icon)

Each theme can specify a different fallback image — the path is saved as part of the
theme's `.cfg` settings.

### Image Format

- **Format:** BMP, 24-bit or 16-bit RGB
- **Recommended size:** 128×128 px (the maximum thumbnail size)
- **Aspect ratio:** Square (1:1). Non-square images will be stretched.

Rockbox scales the image to the current thumbnail size automatically, so one
file covers all size settings.

### When the fallback is cached

A pre-scaled `.bin` is generated in `/.rockbox/thumbcache/Np/` when the Debug →
Album Art Thumbnails build runs. If the fallback `.cfg` key is set but the build
has not been run since, the firmware decodes the BMP on first browse-view access
and caches it for subsequent opens.

### Removing it

Set `album art fallback:` to empty in the `.cfg` (or remove the line). The browser
will revert to the built-in file-type icon for albums with no art.

---

## Removing Old BMP-Based Corners

If your theme previously implemented rounded list corners using a repeating bitmap
overlay (e.g. `LabelEdgeLeft.bmp` drawn via `%Vl`), remove those `%xl` loads and
`%Vl` drawing lines from your `.sbs` and replace them with the `list corner radius`
config key. The firmware implementation is:

- Height-agnostic (works at any row height without per-height bitmap variants)
- Colour-agnostic (reads the viewport background automatically)
- Universal (applies to all list screens, not only the database view)

---

## Example `.cfg` Snippet

```cfg
# --- RockboxMod extensions ---
list corner radius: 6
album art size: 44
album art theme size: 44
album art padding: 2
album art fallback: /.rockbox/themes/mytheme_fallback.bmp
```

All settings are optional. Omit any to leave it at the user's current value
(which defaults to off / auto / 0 / empty if the user has never changed it).

For the fallback thumbnail, include a BMP in your theme package (e.g.
`mytheme_fallback.bmp` alongside your `.cfg`) and reference its full path in
the `album art fallback` key.
