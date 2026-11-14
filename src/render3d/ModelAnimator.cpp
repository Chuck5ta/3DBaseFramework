#include "render3d/ModelAnimator.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace render3d {

void ModelAnimator::play(const Model& model, std::string_view animation, AnimationLoop loop, bool restart)
{
    const bool sameModel = model_ == &model;
    if (sameModel && !restart && current_ >= 0 && model.animations()[size_t(current_)].name == animation)
        return;
    const int index = model.findAnimation(animation);
    if (index < 0)
        return; // findAnimation has warned; keep whatever is playing

    // Blend from what's on screen now (if it's a pose of this model).
    from_ = (sameModel && pose_.size() == size_t(model.nodeCount())) ? pose_ : model.defaultPose();
    blend_ = blendTime > 0.0f ? 0.0f : 1.0f;

    model_ = &model;
    current_ = index;
    loop_ = loop;
    time_ = 0.0f;
    direction_ = 1;
    finished_ = false;
    justStarted_ = true;
    segmentCount_ = 0;
    refresh();
}

void ModelAnimator::stop()
{
    if (!model_)
        return;
    from_ = pose_;
    blend_ = blendTime > 0.0f ? 0.0f : 1.0f;
    current_ = -1;
    finished_ = false;
    segmentCount_ = 0;
    refresh();
}

bool ModelAnimator::isPlaying(std::string_view animation) const
{
    return current_ >= 0 && model_->animations()[size_t(current_)].name == animation;
}

const std::string& ModelAnimator::animationName() const
{
    static const std::string none;
    return current_ >= 0 ? model_->animations()[size_t(current_)].name : none;
}

float ModelAnimator::length() const
{
    return current_ >= 0 ? model_->animations()[size_t(current_)].duration : 0.0f;
}

bool ModelAnimator::reached(float seconds) const
{
    for (int i = 0; i < segmentCount_; ++i)
        if (seconds > segments_[i].from && seconds <= segments_[i].to)
            return true;
    return false;
}

void ModelAnimator::update(float dt)
{
    if (!model_)
        return;
    if (blend_ < 1.0f)
        blend_ = blendTime > 0.0f ? std::min(1.0f, blend_ + dt / blendTime) : 1.0f;

    // Work out the new time, and which stretch of the animation was passed (for reached()).
    segmentCount_ = 0;
    const auto passed = [this](float from, float to) { segments_[segmentCount_++] = {from, to}; };
    constexpr float kBeforeStart = -std::numeric_limits<float>::infinity();
    if (current_ >= 0 && !finished_) {
        const float len = length();
        const float start = justStarted_ ? kBeforeStart : time_;
        const float step = dt * std::max(speed, 0.0f);
        float t = time_ + float(direction_) * step;
        if (len <= 0.0f) { // a single pose
            t = 0.0f;
            passed(start, 0.0f);
            finished_ = loop_ == AnimationLoop::Once;
        } else if (loop_ == AnimationLoop::Once) {
            if (t >= len) {
                t = len;
                finished_ = true;
            }
            passed(start, t);
        } else if (loop_ == AnimationLoop::Loop) {
            if (t >= len) {
                if (step >= len) {
                    passed(kBeforeStart, len); // a whole loop (or more) in one step
                    t = std::fmod(t, len);
                } else {
                    t -= len;
                    passed(start, len);
                    passed(kBeforeStart, t);
                }
            } else {
                passed(start, t);
            }
        } else { // PingPong
            if (direction_ > 0) {
                if (t > len) { // bounce off the end
                    t = std::max(0.0f, 2.0f * len - t);
                    direction_ = -1;
                    passed(std::min(start, t), len);
                } else {
                    passed(start, t);
                }
            } else {
                if (t < 0.0f) { // bounce off the start
                    t = std::min(len, -t);
                    direction_ = 1;
                    passed(kBeforeStart, std::max(time_, t));
                } else {
                    passed(std::nextafter(t, kBeforeStart), time_);
                }
            }
        }
        time_ = t;
    }
    justStarted_ = false;
    refresh();
}

void ModelAnimator::refresh()
{
    ModelPose target = model_->defaultPose();
    if (current_ >= 0)
        model_->animations()[size_t(current_)].apply(time_, target);

    if (blend_ >= 1.0f || from_.size() != target.size()) {
        pose_ = std::move(target);
        return;
    }
    // Smoothstep makes the change of motion gentle at both ends.
    const float w = blend_ * blend_ * (3.0f - 2.0f * blend_);
    for (size_t i = 0; i < target.size(); ++i) {
        const NodeTransform& a = from_.nodes_[i];
        NodeTransform& b = target.nodes_[i];
        b.translation = glm::mix(a.translation, b.translation, w);
        b.rotation = glm::slerp(a.rotation, b.rotation, w);
        b.scale = glm::mix(a.scale, b.scale, w);
    }
    pose_ = std::move(target);
}

} // namespace render3d
