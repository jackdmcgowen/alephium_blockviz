# Alephium-aligned palette (BlockFlow viz)

Grounded in the official [Alephium Brand Guide](https://github.com/alephium/alephium-brand-guide):

- **Default:** white mark on **black** background  
- **Type:** Inter (target for ImGui; system default until font is bundled)  
- **Hero accent:** brand orange (token / CTA energy)  
- Marketing site: dark UI + white type + orange CTAs  

Exact logo SVG geometry is in the brand guide; orange below matches common Alephium token/marketing orange (~`#FF5C00`). Re-sample the brand kit color board if marketing ships a stricter token set.

## Foundation

| Token | Hex | RGB (0–1) | Use |
|-------|-----|-----------|-----|
| `canvas` | `#0B0B0C` | 0.043, 0.043, 0.047 | Vulkan clear |
| `panel` | `#1A1B1E` | 0.102, 0.106, 0.118 | ImGui window |
| `panel_border` | `#2A2C31` | 0.165, 0.173, 0.192 | Borders |
| `text` | `#F5F5F5` | 0.961, 0.961, 0.961 | Primary text |
| `text_muted` | `#9A9A9E` | 0.604, 0.604, 0.620 | Disabled |
| `brand_orange` | `#FF5C00` | 1.000, 0.361, 0.000 | Accents, checkmarks, CTAs |

## Semantic roles (app product meaning)

| Token | Hex / float | Use |
|-------|-------------|-----|
| `tip_green` | `#3DDC84` | Frontier tip Sobel / green arrows |
| `frontier_cyan` | `#2EE6F0` | Cyan frontier-child links |
| `incomplete_amber` | `#FF8A1F` | Incomplete / orange Sobel (not brand CTA) |
| `select_gold` | `#F0C14A` | Selection / gold Sobel |
| `uncle_violet` | `#B847F2` | Ghost uncle tint |
| `death_red` | `#FF1F1A` | Removal fade |

## Shard lanes (16)

Ordered family: **cool slate → brand orange → warm sand**, 4 groups × 4 chains so lanes stay distinct without rainbow primaries. Values live in `LanePalette::default_alephium()`.

## Capture baseline

- Before: `docs/images/capture_current.png` (light canvas, rainbow lanes)  
- After palette: re-capture with **F12** or Network → Blockflow → **Screenshot**
