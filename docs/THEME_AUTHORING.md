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

## Album Art Row Height (Database View Only)

Overrides the row height in the database browser (ID3 tag view) to make room for
album art thumbnails. Has no effect in the file browser or any other screen.

**Config key:** `album art row height`  
**Values:** `auto` (use font height, default), `20`, `24`, `28`, `32`, `36`, `40`, `44`, `48` (pixels)  
**User override:** Theme Settings → Album Art Row Height

```cfg
# 48px rows give comfortable space for a 44px thumbnail:
album art row height: 48
```

The thumbnail size is derived automatically as `row height − 4px`. At `auto`, the
thumbnail size is derived from the font height instead.

Album art is only shown in the **database view** (Artists → Albums → Tracks, etc.),
never in the file browser. One thumbnail per album is cached to disk at
`/.rockbox/thumbcache/`. The cache is built automatically after a database update.

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

| Row Height | Padding | Effective Row Height |
|---|---|---|
| 32 | 0 | 32 (unchanged) |
| 32 | 2 | 36 |
| 32 | 4 | 40 |
| 48 | 3 | 54 |

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
- **Recommended size:** 44×44 px (the maximum thumbnail size)
- **Aspect ratio:** Square (1:1). Non-square images will be stretched.

Rockbox scales the image to the current thumbnail size automatically, so one
file covers every row height setting. Design it to read clearly at small sizes
(as small as 12×12 px when the row height is 16 px).

### When the fallback is cached

A pre-scaled `.bin` is generated in `/.rockbox/thumbcache/` during the database
update (same pass that builds album thumbnails). This means the fallback image is
ready the first time the database browser opens — no BMP decode at browse time.

If the fallback `.cfg` key is set but the database has not been updated since, the
firmware decodes the BMP on first use and caches it for subsequent opens.

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
album art row height: 48
album art padding: 2
album art fallback: /.rockbox/themes/mytheme_fallback.bmp
```

All settings are optional. Omit any to leave it at the user's current value
(which defaults to off / auto / 0 / empty if the user has never changed it).

For the fallback thumbnail, include a BMP in your theme package (e.g.
`mytheme_fallback.bmp` alongside your `.cfg`) and reference its full path in
the `album art fallback` key.
