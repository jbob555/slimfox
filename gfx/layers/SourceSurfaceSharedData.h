/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SOURCESURFACESHAREDDATA_H_
#define MOZILLA_GFX_SOURCESURFACESHAREDDATA_H_

#include "mozilla/gfx/2D.h"
#include "mozilla/Mutex.h"
#include "mozilla/ipc/SharedMemoryBasic.h"

namespace mozilla {
namespace gfx {

class SourceSurfaceSharedData;

/**
 * This class is used to wrap shared (as in process) data buffers allocated by
 * a SourceSurfaceSharedData object. It may live in the same process or a
 * different process from the actual SourceSurfaceSharedData object.
 *
 * If it is in the same process, mBuf is the same object as that in the surface.
 * It is a useful abstraction over just using the surface directly, because it
 * can have a different lifetime from the surface; if the surface gets freed,
 * consumers may continue accessing the data in the buffer. Releasing the
 * original surface is a signal which feeds into SharedSurfacesParent to decide
 * to release the SourceSurfaceSharedDataWrapper.
 *
 * If it is in a different process, mBuf is a new SharedMemoryBasic object which
 * mapped in the given shared memory handle as read only memory.
 */
class SourceSurfaceSharedDataWrapper final : public DataSourceSurface {
  typedef mozilla::ipc::SharedMemoryBasic SharedMemoryBasic;

 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SourceSurfaceSharedDataWrapper,
                                          override)

  SourceSurfaceSharedDataWrapper()
      : mStride(0), mFormat(SurfaceFormat::UNKNOWN) {}

  bool Init(const IntSize& aSize, int32_t aStride, SurfaceFormat aFormat,
            const SharedMemoryBasic::Handle& aHandle,
            base::ProcessId aCreatorPid);

  void Init(SourceSurfaceSharedData* aSurface);

  base::ProcessId GetCreatorPid() const { return mCreatorPid; }

  int32_t Stride() override { return mStride; }

  SurfaceType GetType() const override { return SurfaceType::DATA; }
  IntSize GetSize() const override { return mSize; }
  SurfaceFormat GetFormat() const override { return mFormat; }

  uint8_t* GetData() override { return static_cast<uint8_t*>(mBuf->memory()); }

  bool OnHeap() const override { return false; }

  bool Map(MapType, MappedSurface* aMappedSurface) override {
    aMappedSurface->mData = GetData();
    aMappedSurface->mStride = mStride;
    return true;
  }

  void Unmap() override {}

  bool AddConsumer() { return ++mConsumers == 1; }

  bool RemoveConsumer() {
    MOZ_ASSERT(mConsumers > 0);
    return --mConsumers == 0;
  }

 private:
  size_t GetDataLength() const {
    return static_cast<size_t>(mStride) * mSize.height;
  }

  size_t GetAlignedDataLength() const {
    return mozilla::ipc::SharedMemory::PageAlignedSize(GetDataLength());
  }

  int32_t mStride;
  uint32_t mConsumers;
  IntSize mSize;
  RefPtr<SharedMemoryBasic> mBuf;
  SurfaceFormat mFormat;
  base::ProcessId mCreatorPid;
};

/**
 * This class is used to wrap shared (as in process) data buffers used by a
 * source surface.
 */
class SourceSurfaceSharedData final : public DataSourceSurface {
  typedef mozilla::ipc::SharedMemoryBasic SharedMemoryBasic;

 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SourceSurfaceSharedData, override)

  SourceSurfaceSharedData()
      : mMutex("SourceSurfaceSharedData"),
        mStride(0),
        mMapCount(0),
        mHandleCount(0),
        mInvalidations(0),
        mFormat(SurfaceFormat::UNKNOWN),
        mClosed(false),
        mFinalized(false),
        mShared(false) {}

  /**
   * Initialize the surface by creating a shared memory buffer with a size
   * determined by aSize, aStride and aFormat. If aShare is true, it will also
   * immediately attempt to share the surface with the GPU process via
   * SharedSurfacesChild.
   */
  bool Init(const IntSize& aSize, int32_t aStride, SurfaceFormat aFormat,
            bool aShare = true);

  uint8_t* GetData() override {
    MutexAutoLock lock(mMutex);
    return GetDataInternal();
  }

  int32_t Stride() override { return mStride; }

  SurfaceType GetType() const override { return SurfaceType::DATA_SHARED; }
  IntSize GetSize() const override { return mSize; }
  SurfaceFormat GetFormat() const override { return mFormat; }

  void GuaranteePersistance() override;

  void AddSizeOfExcludingThis(MallocSizeOf aMallocSizeOf, size_t& aHeapSizeOut,
                              size_t& aNonHeapSizeOut,
                              size_t& aExtHandlesOut) const override;

  bool OnHeap() const override { return false; }

  /**
   * Although Map (and Moz2D in general) isn't normally threadsafe,
   * we want to allow it for SourceSurfaceSharedData since it should
   * always be fine (for reading at least).
   *
   * This is the same as the base class implementation except using
   * mMapCount instead of mIsMapped since that breaks for multithread.
   *
   * Additionally if a reallocation happened while there were active
   * mappings, then we guarantee that GetData will continue to return
   * the same data pointer by retaining the old shared buffer until
   * the last mapping is freed via Unmap.
   */
  bool Map(MapType, MappedSurface* aMappedSurface) override {
    MutexAutoLock lock(mMutex);
    ++mMapCount;
    aMappedSurface->mData = GetDataInternal();
    aMappedSurface->mStride = mStride;
    return true;
  }

  void Unmap() override {
    MutexAutoLock lock(mMutex);
    MOZ_ASSERT(mMapCount > 0);
    if (--mMapCount == 0) {
      mOldBuf = nullptr;
    }
  }

  /**
   * Get a handle to share to another process for this buffer. Returns:
   *   NS_OK -- success, aHandle is valid.
   *   NS_ERROR_NOT_AVAILABLE -- handle was closed, need to reallocate.
   *   NS_ERROR_FAILURE -- failed to create a handle to share.
   */
  nsresult ShareToProcess(base::ProcessId aPid,
                          SharedMemoryBasic::Handle& aHandle);

  /**
   * Indicates the buffer is not expected to be shared with any more processes.
   * May release the handle if possible (see CloseHandleInternal).
   */
  void FinishedSharing() {
    MutexAutoLock lock(mMutex);
    mShared = true;
    CloseHandleInternal();
  }

  /**
   * Indicates whether or not the buffer can be shared with another process
   * without reallocating. Note that this is racy and should only be used for
   * informational/reporting purposes.
   */
  bool CanShare() const {
    MutexAutoLock lock(mMutex);
    return !mClosed;
  }

  /**
   * Allocate a new shared memory buffer so that we can get a new handle for
   * sharing to new processes. ShareToProcess must have failed with
   * NS_ERROR_NOT_AVAILABLE in order for this to be safe to call. Returns true
   * if the operation succeeds. If it fails, there is no state change.
   */
  bool ReallocHandle();

  /**
   * Signals we have finished writing to the buffer and it may be marked as
   * read only.
   */
  void Finalize();

  /**
   * Indicates whether or not the buffer can change. If this returns true, it is
   * guaranteed to continue to do so for the remainder of the surface's life.
   */
  bool IsFinalized() const {
    MutexAutoLock lock(mMutex);
    return mFinalized;
  }

  /**
   * Indicates how many times the surface has been invalidated.
   */
  int32_t Invalidations() const override {
    MutexAutoLock lock(mMutex);
    return mInvalidations;
  }

  /**
   * Increment the invalidation counter.
   */
  void Invalidate() override {
    MutexAutoLock lock(mMutex);
    ++mInvalidations;
    MOZ_ASSERT(mInvalidations >= 0);
  }

  /**
   * While a HandleLock exists for the given surface, the shared memory handle
   * cannot be released.
   */
  class MOZ_STACK_CLASS HandleLock final {
   public:
    explicit HandleLock(SourceSurfaceSharedData* aSurface)
        : mSurface(aSurface) {
      mSurface->LockHandle();
    }

    ~HandleLock() { mSurface->UnlockHandle(); }

   private:
    RefPtr<SourceSurfaceSharedData> mSurface;
  };

 private:
  friend class SourceSurfaceSharedDataWrapper;

  ~SourceSurfaceSharedData() override { MOZ_ASSERT(mMapCount == 0); }

  void LockHandle() {
    MutexAutoLock lock(mMutex);
    ++mHandleCount;
  }

  void UnlockHandle() {
    MutexAutoLock lock(mMutex);
    MOZ_ASSERT(mHandleCount > 0);
    --mHandleCount;
    mShared = true;
    CloseHandleInternal();
  }

  uint8_t* GetDataInternal() const;

  size_t GetDataLength() const {
    return static_cast<size_t>(mStride) * mSize.height;
  }

  size_t GetAlignedDataLength() const {
    return mozilla::ipc::SharedMemory::PageAlignedSize(GetDataLength());
  }

  /**
   * Attempt to close the handle. Only if the buffer has been both finalized
   * and we have completed sharing will it be released.
   */
  void CloseHandleInternal();

  mutable Mutex mMutex;
  int32_t mStride;
  int32_t mMapCount;
  int32_t mHandleCount;
  int32_t mInvalidations;
  IntSize mSize;
  RefPtr<SharedMemoryBasic> mBuf;
  RefPtr<SharedMemoryBasic> mOldBuf;
  SurfaceFormat mFormat;
  bool mClosed : 1;
  bool mFinalized : 1;
  bool mShared : 1;
};

}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_SOURCESURFACESHAREDDATA_H_ */