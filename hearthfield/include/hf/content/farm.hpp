// The board's dimensions and the assets it is made of — declarative data.
//
// Sizes are in METRES, like lantern and tideworn. They live in `content`
// rather than in view/ because the SIM needs some of them too: a pick box is
// as tall as the crop standing on the plot, and picking is geometry the sim
// owns (hf/runtime/grid.hpp).
#pragma once

#include <string_view>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"

namespace hearthfield::content {

// THE WORLD PATHS MOVED to hf/content/islands.hpp on 2026-08-29 (sky world s3),
// and the split changed shape with them. There were two chunks here, divided by
// AUTHORING lifecycle - the steading a person edits, and bulk decoration. There
// are now three, divided by LOAD lifetime, because travel has to be able to
// unload one and keep another: `worlds/persistent.world.json` (camera, sun,
// sky), one island chunk, and that island's zone. Hearthfield's farm is the hub
// island's zone; `content::kIslands` names all three.
inline constexpr std::string_view kTileModel = "models/tile.glb";
// The plot surface. Furrows are a MATERIAL, not geometry: the tile has 40
// triangles for its bevel, and the 0.08 m gap between neighbours already draws
// the grid (art bible 6.1). tile.glb carries a top-down UV projection so this
// covers the top face exactly once.
inline constexpr std::string_view kSoilTexture = "textures/tilled_soil.png";
// Unbought land. The MAJORITY surface — a fresh farm owns 4 plots of 576 — and
// it is the one that had no art until 2026-08-28. Bare clods and stones with no
// direction, because furrows are what makes soil read as WORKED.
inline constexpr std::string_view kLockedTexture = "textures/bare_earth.png";
// ONE MESH PER CROP, serving EVERY growth stage: growth is a per-instance
// uniform scale, which is why a stage costs no draw call. Wheat and corn are
// told apart by SILHOUETTE, never by colour - the board overrides a crop's
// material to say ripe-versus-growing, and that read is what colour is for.
// Both are authored at true metric size with a base pivot (ADR-0135), so unlike
// the cone they replaced the view scales them by growth ALONE.
inline constexpr std::string_view kWheatModel = "models/wheat.gltf";
inline constexpr std::string_view kCornModel = "models/corn.gltf";

inline constexpr aether::F32 kCellSize = 1.0f;
// 8 -> 24 on 2026-08-28, AND IT IS A SCENE RE-LAYOUT RATHER THAN A CONSTANT
// (event:2026-08-28#24). Everything in the steading was authored just outside a
// field whose half-extent was 4 m, so a 12 m one puts the mill, coop, yard,
// fence and track ON the plots, and `kRingColumns` was SMALLER than the new
// board. All of it moved together; the mill is what caps it, occupying x = 5..7
// as a 2-cell span, so nothing above 10 columns was possible without this.
// COMPOSITION is the reason: the hub island's plateau is
// ~68 m of usable ground and an 8 m farm on it reads as a postage stamp — the
// island was replaced the same day and the farm became the weakest thing on
// screen. 24 m is a farm you can see.
//
// The cost was measured on a board that is actually PLAYED rather than one that
// is freshly started, which took fixing `--sow` first (event:2026-08-28#20): a
// fully-owned 24x24 is 131,724 triangles and 59 draws of 70 at 0.888 ms,
// or 5.3% of a 16.67 ms frame. DRAWS are the gate and they barely move, exactly
// as ADR-0136 says; the triangle bound was measuring a board with no crops on
// it.
//
// THE SAVE IS NOT AT RISK — `save.hpp` writes the column count per farm and
// validates it on read, so an existing 8-column farm stays 8 forever and only a
// NEW farm gets this. What IS at risk is PACING: this is nine times the plots,
// and nothing tests how a farm feels.
inline constexpr aether::U32 kDefaultColumns = 24;

// A soil tile is authored at TRUE SIZE with its pivot at the base centre
// (ADR-0135), so tile.glb is (kCellSize - kTileGap) across and kTileHeight tall
// and the view scales it by 1. The gap keeps the grid legible from above, which
// under a parallel projection is the only cue that the plots are separate
// objects rather than one textured plane.
inline constexpr aether::F32 kTileGap = 0.08f;
inline constexpr aether::F32 kTileHeight = 0.12f;

// A fully grown crop. The cone primitive is a unit shape, so these ARE the
// scale applied to it — and growth multiplies them, which is why a growth
// stage costs no draw call (the plan's §3b).
inline constexpr aether::F32 kCropHeight = 0.8f;
inline constexpr aether::F32 kCropRadius = 0.33f;

// Nothing shrinks below this. A crop sown one tick ago would otherwise be a
// zero-height sliver that reads as an empty plot, and the player would think
// the tap missed.
inline constexpr aether::F32 kMinGrowth = 0.15f;

// ---- the light ------------------------------------------------------------
//
// The AMBIENT term, in LINEAR RGB like every colour in farm.world.json. The
// engine's default is {0.10, 0.115, 0.15}, a dim cool blue meant for a generic
// scene, and Hearthfield never set it: the farm has one hard directional key
// and no fill, so every shadow side fell to that near-black blue and fought a
// palette built out of warm soil and cream.
//
// This is a CONSTANT ambient rather than an IBL environment, deliberately.
// GEA 11.3.3.3 frames environment maps as producing reflections on "highly
// specular (shiny)" surfaces, and every material here is metallic 0 with
// roughness 0.8-0.95, so the specular half of a split-sum term would buy
// almost nothing for the cost of a bake, a cube map and a sampler slot. The
// DIFFUSE half is what an outdoor scene wants, and a single sky-toned value is
// most of it under a parallel projection where nothing turns to catch a
// gradient. Revisit when a material here is actually glossy - water, glass, a
// metal roof.
//
// MEASURED, not chosen (1280x720, grid 8, frame 110). Against the engine
// default the shadow-to-grass luminance ratio goes 0.257 -> 0.449 while lit
// grass rises only 7%, so the lift lands in the shadows where it belongs. The
// BLUE channel is 0.26 and not 0.34 for a reason worth keeping: at 0.34 the
// same lift cost saturation, grass falling from 0.241 to 0.218, because a
// blue-dominant ambient adds blue to every surface in a palette made of warm
// soil and cream. Matching ambient to the ground's own hue - most of what these
// surfaces actually see bounced at them is grass - restores it to 0.247, very
// slightly above where it started.
inline constexpr aether::Vec3 kAmbient{0.26f, 0.30f, 0.26f};

// ---- the steading ----------------------------------------------------------
//
// THE POSITIONS ARE GONE FROM HERE (H6, spec check 3). The mill, the coop and
// the flock's yard are authored in farm.world.json and found by NAME — these
// are the names, and they are the whole remaining contract between the file and
// the code. Until H6 they were metre constants offset from the board's size,
// which meant the farm's layout lived in C++ and the editor could not touch it.
//
// A name is GEA §16.3.3.1's UniqueId. `scene::FindEntityNode` resolves one; a
// miss is reported, never guessed at.
inline constexpr std::string_view kMillEntity = "mill";
inline constexpr std::string_view kCoopEntity = "coop";
inline constexpr std::string_view kYardEntity = "yard";

// How far the birds stand from the yard locator. STILL CODE, and deliberately:
// it is a function of the flock's size, which is content::kFlockSize and
// therefore the sim's — the same line H1 drew for the plot table. Authoring
// four bird positions would be a second copy of a number the sim decides.
inline constexpr aether::F32 kYardRadius = 0.8f;

}  // namespace hearthfield::content
