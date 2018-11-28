/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "APM::EffectDescriptor"
//#define LOG_NDEBUG 0

#include "EffectDescriptor.h"
#include <utils/String8.h>

namespace android {

void EffectDescriptor::dump(String8 *dst, int spaces) const
{
    dst->appendFormat("%*sI/O: %d\n", spaces, "", mIo);
    dst->appendFormat("%*sStrategy: %d\n", spaces, "", mStrategy);
    dst->appendFormat("%*sSession: %d\n", spaces, "", mSession);
    dst->appendFormat("%*sName: %s\n", spaces, "",  mDesc.name);
    dst->appendFormat("%*s%s\n", spaces, "",  mEnabled ? "Enabled" : "Disabled");
}

EffectDescriptorCollection::EffectDescriptorCollection() :
    mTotalEffectsCpuLoad(0),
    mTotalEffectsMemory(0),
    mTotalEffectsMemoryMaxUsed(0)
{

}

status_t EffectDescriptorCollection::registerEffect(const effect_descriptor_t *desc,
                                                    audio_io_handle_t io,
                                                    uint32_t strategy,
                                                    int session,
                                                    int id)
{
    if (getEffect(id) != nullptr) {
        ALOGW("%s effect %s already registered", __FUNCTION__, desc->name);
        return INVALID_OPERATION;
    }

    if (mTotalEffectsMemory + desc->memoryUsage > getMaxEffectsMemory()) {
        ALOGW("registerEffect() memory limit exceeded for Fx %s, Memory %d KB",
                desc->name, desc->memoryUsage);
        return INVALID_OPERATION;
    }
    mTotalEffectsMemory += desc->memoryUsage;
    if (mTotalEffectsMemory > mTotalEffectsMemoryMaxUsed) {
        mTotalEffectsMemoryMaxUsed = mTotalEffectsMemory;
    }
    ALOGV("registerEffect() effect %s, io %d, strategy %d session %d id %d",
            desc->name, io, strategy, session, id);
    ALOGV("registerEffect() memory %d, total memory %d", desc->memoryUsage, mTotalEffectsMemory);

    sp<EffectDescriptor> effectDesc = new EffectDescriptor();
    memcpy (&effectDesc->mDesc, desc, sizeof(effect_descriptor_t));
    effectDesc->mId = id;
    effectDesc->mIo = io;
    effectDesc->mStrategy = static_cast<routing_strategy>(strategy);
    effectDesc->mSession = session;
    effectDesc->mEnabled = false;

    add(id, effectDesc);

    return NO_ERROR;
}

sp<EffectDescriptor> EffectDescriptorCollection::getEffect(int id) const
{
    ssize_t index = indexOfKey(id);
    if (index < 0) {
        return nullptr;
    }
    return valueAt(index);
}

status_t EffectDescriptorCollection::unregisterEffect(int id)
{
    sp<EffectDescriptor> effectDesc = getEffect(id);
    if (effectDesc == nullptr) {
        ALOGW("%s unknown effect ID %d", __FUNCTION__, id);
        return INVALID_OPERATION;
    }

    if (mTotalEffectsMemory < effectDesc->mDesc.memoryUsage) {
        ALOGW("unregisterEffect() memory %d too big for total %d",
                effectDesc->mDesc.memoryUsage, mTotalEffectsMemory);
        effectDesc->mDesc.memoryUsage = mTotalEffectsMemory;
    }
    mTotalEffectsMemory -= effectDesc->mDesc.memoryUsage;
    ALOGV("unregisterEffect() effect %s, ID %d, memory %d total memory %d",
            effectDesc->mDesc.name, id, effectDesc->mDesc.memoryUsage, mTotalEffectsMemory);

    removeItem(id);

    return NO_ERROR;
}

status_t EffectDescriptorCollection::setEffectEnabled(int id, bool enabled)
{
    ssize_t index = indexOfKey(id);
    if (index < 0) {
        ALOGW("unregisterEffect() unknown effect ID %d", id);
        return INVALID_OPERATION;
    }

    return setEffectEnabled(valueAt(index), enabled);
}

bool EffectDescriptorCollection::isEffectEnabled(int id) const
{
    ssize_t index = indexOfKey(id);
    if (index < 0) {
        return false;
    }
    return valueAt(index)->mEnabled;
}

status_t EffectDescriptorCollection::setEffectEnabled(const sp<EffectDescriptor> &effectDesc,
                                                      bool enabled)
{
    if (enabled == effectDesc->mEnabled) {
        ALOGV("setEffectEnabled(%s) effect already %s",
             enabled?"true":"false", enabled?"enabled":"disabled");
        return INVALID_OPERATION;
    }

    if (enabled) {
        if (mTotalEffectsCpuLoad + effectDesc->mDesc.cpuLoad > getMaxEffectsCpuLoad()) {
            ALOGW("setEffectEnabled(true) CPU Load limit exceeded for Fx %s, CPU %f MIPS",
                 effectDesc->mDesc.name, (float)effectDesc->mDesc.cpuLoad/10);
            return INVALID_OPERATION;
        }
        mTotalEffectsCpuLoad += effectDesc->mDesc.cpuLoad;
        ALOGV("setEffectEnabled(true) total CPU %d", mTotalEffectsCpuLoad);
    } else {
        if (mTotalEffectsCpuLoad < effectDesc->mDesc.cpuLoad) {
            ALOGW("setEffectEnabled(false) CPU load %d too high for total %d",
                    effectDesc->mDesc.cpuLoad, mTotalEffectsCpuLoad);
            effectDesc->mDesc.cpuLoad = mTotalEffectsCpuLoad;
        }
        mTotalEffectsCpuLoad -= effectDesc->mDesc.cpuLoad;
        ALOGV("setEffectEnabled(false) total CPU %d", mTotalEffectsCpuLoad);
    }
    effectDesc->mEnabled = enabled;
    return NO_ERROR;
}

bool EffectDescriptorCollection::isNonOffloadableEffectEnabled() const
{
    for (size_t i = 0; i < size(); i++) {
        sp<EffectDescriptor> effectDesc = valueAt(i);
        if (effectDesc->mEnabled && (effectDesc->mStrategy == STRATEGY_MEDIA) &&
                ((effectDesc->mDesc.flags & EFFECT_FLAG_OFFLOAD_SUPPORTED) == 0)) {
            ALOGV("isNonOffloadableEffectEnabled() non offloadable effect %s enabled on session %d",
                  effectDesc->mDesc.name, effectDesc->mSession);
            return true;
        }
    }
    return false;
}

uint32_t EffectDescriptorCollection::getMaxEffectsCpuLoad() const
{
    return MAX_EFFECTS_CPU_LOAD;
}

uint32_t EffectDescriptorCollection::getMaxEffectsMemory() const
{
    return MAX_EFFECTS_MEMORY;
}

void EffectDescriptorCollection::dump(String8 *dst, int spaces, bool verbose) const
{
    if (verbose) {
        dst->appendFormat(
            "\n%*sTotal Effects CPU: %f MIPS, "
            "Total Effects memory: %d KB, Max memory used: %d KB\n",
            spaces, "",
            (float) mTotalEffectsCpuLoad / 10,
            mTotalEffectsMemory,
            mTotalEffectsMemoryMaxUsed);
    }
    dst->appendFormat("%*sEffects:\n", spaces, "");
    for (size_t i = 0; i < size(); i++) {
        dst->appendFormat("%*s- Effect %d:\n", spaces, "", keyAt(i));
        valueAt(i)->dump(dst, spaces + 2);
    }
}

}; //namespace android
