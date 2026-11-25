#pragma once
#include <cstdint>

// Unconditional render diagnostics for the character creation screen.
//
// Every build writes these lines to ux0:/data/mh3-wii/mh3.log through
// MH3DiagLog (diag_log.h), which appends and syncs each line so a log pulled
// while the game runs still holds them. They are cheap: atomic counters, plus
// one report about a second while character creation is active, a heartbeat
// otherwise, and the first few dispatch failures.
namespace RenderDiag {
enum Counter {
    // Scene queue
    PlayerFiltered,
    VisibilityRejected,
    Enqueued,
    SceneDrawn,
    SceneSkipped,
    // Dispatch (model_scope.cpp)
    Dispatch,
    DispatchInvisible,
    DispatchNoShape,
    DispatchNoSources,
    DispatchNoFrame,
    DispatchRecorded,
    // Submission (native_material.cpp)
    Submit,
    SubmitEmpty,
    SubmitNoFrame,
    SubmitAssetFailed,
    SubmitCulled,
    SubmitDrawn,
    // Inside SubmitShadedModelMesh every one of these abandons the draw after
    // SubmitDrawn was counted: no uniform slot, no view block, no material
    // block, or no shaded-buffer block. A model that draws nothing while its
    // submit counters keep rising shows up here.
    SubmitNoUniforms,
    SubmitNoView,
    SubmitNoMaterial,
    SubmitNoBlocks,
    CounterCount
};

#ifdef MH3_DEVELOPMENT
void Count(Counter counter, uint32_t amount = 1);

// True while the first few distinct dispatch failures still have room; the
// caller then resolves the reason (owner, prepared mesh) before NoteDispatch.
bool WantNote();

// A dispatch that resolved a shape but could not draw it; prints the first few
// distinct failures with the reason the native path gives.
void NoteDispatch(uint32_t material_reference, uint32_t shape_reference, uint32_t shape, uint32_t model, const char* reason);

// One model shape the scene dispatched: the census of what is actually drawn
// this second, keyed by the model's address, with the shapes it covers.
void NoteModel(uint32_t model, uint32_t shape, uint32_t index_count, uint32_t skin_rows);

// True once per resident model (up to eight): the first pose a model submits
// and the first draw it reaches. Keyed by the residency handle, not by a
// submission count, so the probes cover every model the scene draws; keyed by
// handle rather than catalog id because separate packs number their models
// independently.
bool WantPoseProbe(uintptr_t model);
bool WantDrawProbe(uintptr_t model);

// True once per model (up to eight) while character creation is active: the
// CalcWorld probe prints the model's scaling rule and, for every node of its
// NodeTree, the parent, flags, static translation and the resulting world
// translation and scale, which pins down a mis-scaled or misplaced part.
bool WantCalcWorldProbe(uint32_t mdl);

// Called once per drawn scene; writes a report immediately on the first scene
// and on entering character creation, about once a second while it is active,
// and a heartbeat otherwise.
void Frame();

// Record last fog state, lighting state, and fullscreen/fade overlays for diagnostics.
void NoteFog(uint32_t type, float start, float end, float near_plane, float far_plane, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void NoteLighting(uint32_t valid_mask, uint32_t diff_color, uint32_t spec_color, uint8_t amb_r, uint8_t amb_g, uint8_t amb_b, uint8_t amb_a,
                  const float light_colors[8][4], const float light_dirs[8][3]);
void NoteBlendDraw(uint32_t blend_mode, uint32_t src_factor, uint32_t dst_factor, uint8_t depth_test, uint8_t textures, uint8_t lit, uint8_t model, uint32_t model_id, uint32_t shape_id, uint32_t triangles);
void NoteOverlay(const char* kind, uint32_t mode, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
#else
inline void Count(Counter, uint32_t = 1) {}
inline bool WantNote() { return false; }
inline void NoteDispatch(uint32_t, uint32_t, uint32_t, uint32_t, const char*) {}
inline void NoteModel(uint32_t, uint32_t, uint32_t, uint32_t) {}
inline bool WantPoseProbe(uintptr_t) { return false; }
inline bool WantDrawProbe(uintptr_t) { return false; }
inline bool WantCalcWorldProbe(uint32_t) { return false; }
inline void Frame() {}
inline void NoteFog(uint32_t, float, float, float, float, uint8_t, uint8_t, uint8_t, uint8_t) {}
inline void NoteLighting(uint32_t, uint32_t, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t, const float[8][4], const float[8][3]) {}
inline void NoteBlendDraw(uint32_t, uint32_t, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t, uint32_t) {}
inline void NoteOverlay(const char*, uint32_t, float, float, uint8_t, uint8_t, uint8_t, uint8_t) {}
#endif
} // namespace RenderDiag
