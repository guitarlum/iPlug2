/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for more info.

 ==============================================================================
*/

#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace iplug
{

// VoLum: RtAudio driver buffers are not guaranteed to be a multiple of the
// standalone processing quantum. This helper carries partial input/output
// vectors across callbacks so the plug-in still always receives fixed-size
// AppProcess() blocks.
class VoLumIPlugAPPVectorAccumulator
{
public:
  void Reset(int nInputs, int nOutputs, int vectorSize)
  {
    mVectorSize = std::max(1, vectorSize);
    mInputWriteIdx = 0;
    mOutputReadIdx = 0;
    mOutputAvailable = false;

    mInputBuffers.assign(std::max(0, nInputs), std::vector<double>(mVectorSize, 0.0));
    mOutputBuffers.assign(std::max(0, nOutputs), std::vector<double>(mVectorSize, 0.0));
    mInputPtrs.resize(mInputBuffers.size());
    mOutputPtrs.resize(mOutputBuffers.size());
    RefreshPtrs();
  }

  void Clear()
  {
    for (auto& buffer : mInputBuffers)
      std::fill(buffer.begin(), buffer.end(), 0.0);
    for (auto& buffer : mOutputBuffers)
      std::fill(buffer.begin(), buffer.end(), 0.0);

    mInputWriteIdx = 0;
    mOutputReadIdx = 0;
    mOutputAvailable = false;
    RefreshPtrs();
  }

  int GetVectorSize() const { return mVectorSize; }
  int GetInputWriteIndex() const { return mInputWriteIdx; }
  int GetOutputReadIndex() const { return mOutputReadIdx; }
  bool HasOutput() const { return mOutputAvailable; }
  int GetNumInputs() const { return static_cast<int>(mInputBuffers.size()); }
  int GetNumOutputs() const { return static_cast<int>(mOutputBuffers.size()); }

  double* GetInputChannel(int channel)
  {
    return mInputBuffers[static_cast<size_t>(channel)].data();
  }

  double* GetOutputChannel(int channel)
  {
    return mOutputBuffers[static_cast<size_t>(channel)].data();
  }

  double** GetInputPtrs()
  {
    RefreshPtrs();
    return mInputPtrs.empty() ? nullptr : mInputPtrs.data();
  }

  double** GetOutputPtrs()
  {
    RefreshPtrs();
    return mOutputPtrs.empty() ? nullptr : mOutputPtrs.data();
  }

  bool PushInputFrame()
  {
    ++mInputWriteIdx;
    if (mInputWriteIdx >= mVectorSize)
    {
      mInputWriteIdx = 0;
      return true;
    }
    return false;
  }

  void CommitProcessedOutput()
  {
    mOutputReadIdx = 0;
    mOutputAvailable = !mOutputBuffers.empty();
  }

  void PopOutputFrame()
  {
    if (!mOutputAvailable)
      return;

    ++mOutputReadIdx;
    if (mOutputReadIdx >= mVectorSize)
    {
      mOutputReadIdx = 0;
      mOutputAvailable = false;
    }
  }

private:
  void RefreshPtrs()
  {
    for (size_t i = 0; i < mInputBuffers.size(); ++i)
      mInputPtrs[i] = mInputBuffers[i].data();
    for (size_t i = 0; i < mOutputBuffers.size(); ++i)
      mOutputPtrs[i] = mOutputBuffers[i].data();
  }

  int mVectorSize = 1;
  int mInputWriteIdx = 0;
  int mOutputReadIdx = 0;
  bool mOutputAvailable = false;
  std::vector<std::vector<double>> mInputBuffers;
  std::vector<std::vector<double>> mOutputBuffers;
  std::vector<double*> mInputPtrs;
  std::vector<double*> mOutputPtrs;
};

} // namespace iplug
