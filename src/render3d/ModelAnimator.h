#pragma once

// Plays a model's animations (Blender Actions exported in its glTF file) and keeps the
// resulting pose. Works like render2d::Animator: each animated object has its own.
//
//   // onInit
//   soldier_ = render3d::Model::load(app.gpu(), core::assetPath("models/gunner.glb"));
//   animator_.play(soldier_, "Idle");                         // loops
//
//   // a key press
//   animator_.play(soldier_, "Fire", render3d::AnimationLoop::Once);
//
//   // onUpdate
//   animator_.update(dt);
//   if (animator_.reached(0.05f)) ...                          // a moment in the clip
//   if (animator_.isPlaying("Fire") && animator_.finished())
//       animator_.play(soldier_, "Idle");
//
//   // onRender
//   meshes.draw(soldier_, transform, &animator_.pose());
//
// Changing clip blends smoothly from the old pose to the new one over `blendTime`.
// The pose can be adjusted after update() (aim the head at a target, say) before drawing.
//
// The animator points at the Model it plays, so keep the model alive and in place (a
// member loaded in onInit is ideal) while animators use it.

#include "render3d/Model.h"

#include <string>
#include <string_view>

namespace render3d {

enum class AnimationLoop {
    Loop,     // repeats
    Once,     // plays to the end and holds the last pose; finished() becomes true
    PingPong, // forwards, backwards, forwards...
};

class ModelAnimator {
public:
    // Starts an animation. Asking for the one that's already playing does nothing (so it
    // is safe to call every frame), unless `restart` is true. An unknown name logs a
    // warning (once) listing the model's animations, and the current one carries on.
    void play(const Model& model, std::string_view animation, AnimationLoop loop = AnimationLoop::Loop,
              bool restart = false);
    // Blends back to the model's default pose (as exported) and stops.
    void stop();

    // Advances time. Call once per frame (onUpdate) or per step (onFixedUpdate).
    void update(float dt);

    float speed = 1.0f;      // 2 = twice as fast; 0 = paused
    float blendTime = 0.15f; // seconds to blend from the previous animation (0 = cut)

    bool playing() const { return current_ >= 0; }
    bool isPlaying(std::string_view animation) const;
    const std::string& animationName() const;
    bool finished() const { return finished_; } // a Once animation reached its end
    float time() const { return time_; }        // seconds into the animation
    float length() const;                       // seconds; 0 if nothing is playing

    // True if the animation passed `seconds` during the last update(): use it for moments
    // such as a muzzle flash or a footstep. Also true at 0 on the first update after play().
    bool reached(float seconds) const;

    // The pose to draw with. Empty (the model's own default pose) until play() is called.
    const ModelPose& pose() const { return pose_; }
    ModelPose& pose() { return pose_; }

private:
    void refresh(); // recomputes pose_ from the animations and the blend

    const Model* model_ = nullptr;
    int current_ = -1;
    AnimationLoop loop_ = AnimationLoop::Loop;
    float time_ = 0.0f;
    int direction_ = 1;          // ping-pong: +1 forwards, -1 backwards
    bool finished_ = false;
    bool justStarted_ = false;   // the next update() starts the animation (reached(0) is true)

    // The stretches of the animation passed in the last update() (two when it looped).
    struct Segment { float from, to; }; // from (exclusive) .. to (inclusive), in seconds
    Segment segments_[2]{};
    int segmentCount_ = 0;

    ModelPose from_;     // the pose being blended away from
    float blend_ = 1.0f; // 0 = all `from_`, 1 = all the current animation
    ModelPose pose_;
};

} // namespace render3d
