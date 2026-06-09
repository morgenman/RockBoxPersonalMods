# Theme Authoring — RockboxMod Extensions

Themes can set the following keys in their `.cfg` file. All keys are optional.

---

## `list corner radius`

Draws rounded corners on list items.

**Values:** `0` (default), `2`, `4`, `6`, `8`, `10`, `12` (pixels)  
**User override:** Theme Settings → List Corner Radius

---

## `album art theme size`

Suggests a thumbnail pixel size for the database browser when the user has "Album Art Size" set to Auto. Has no effect if the user has chosen an explicit size.

**Values:** `0` (unset, default), or any art size in pixels (e.g. `36`, `44`, `64`)  

Row height in the database browser is always `art size + 4px`.

---

## `album art padding`

Adds inset space around the artwork inside its column. Useful when corner radius clips artwork.

**Values:** `0`–`6` (pixels per side, default `0`)  
**User override:** Theme Settings → Album Art Padding

| Art size | Padding | Row height |
|---|---|---|
| 28 | 0 | 32 |
| 28 | 2 | 36 |
| 28 | 4 | 40 |
| 44 | 3 | 54 |

---

## `album art fallback`

BMP shown in the database browser for albums with no embedded art and no folder image.

**Value:** Full path to a BMP file, or empty (default — shows file-type icon)  
**Format:** BMP 24-bit or 16-bit RGB, square aspect ratio  
**Max size:** 128×128 px

The image is scaled to the current thumbnail size at cache build time (Debug → Album Art Thumbnails) or on first access if the cache has not been built.

---

## Example `.cfg`

```cfg
list corner radius: 6
album art theme size: 44
album art padding: 2
album art fallback: /.rockbox/themes/mytheme_fallback.bmp
```
