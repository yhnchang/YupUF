#include "UnityTexture.h"

UnityTexture::UnityTexture(int id, void* texPtr, int width, int height, const UnityPlugin &plugin)
	: mId(id)
	, mUnityPlugin(plugin)
	, mWidth(width)
	, mHeight(height)
	, mTexturePointer(texPtr)
{
}


UnityTexture::~UnityTexture()
{
}

void UnityTexture::update(const FrameBuffer * buffer)
{
	if (!buffer)
		return;

#if SUPPORT_D3D9
	// D3D9 case
	if (mUnityPlugin.deviceType() == kUnityGfxRendererD3D9)
	{
		// Update native texture from code
		if (mTexturePointer)
		{
			IDirect3DTexture9* d3dtex = (IDirect3DTexture9*)mTexturePointer;
			D3DSURFACE_DESC desc;
			d3dtex->GetLevelDesc(0, &desc);
			D3DLOCKED_RECT lr;
			d3dtex->LockRect(0, &lr, NULL, 0);
			FillBuffer((unsigned char*)lr.pBits, buffer, desc.Width, desc.Height, lr.Pitch);
			d3dtex->UnlockRect(0);
		}
	}
#endif


#if SUPPORT_D3D11
	// D3D11 case
	if (mUnityPlugin.deviceType() == kUnityGfxRendererD3D11)
	{
		ID3D11DeviceContext* ctx = NULL;
		mUnityPlugin.getD3D11Device()->GetImmediateContext(&ctx);

		// update native texture from code
		if (mTexturePointer)
		{
			ID3D11Texture2D* d3dtex = (ID3D11Texture2D*)mTexturePointer;
			//D3D11_TEXTURE2D_DESC desc;
			//d3dtex->GetDesc (&desc);

			ctx->UpdateSubresource(d3dtex, 0, NULL, buffer->cst_data(), buffer->pitch(), 0);

		}

		ctx->Release();
	}
#endif

#if SUPPORT_D3D12
	// D3D12 case
	if (s_DeviceType == kUnityGfxRendererD3D12)
	{
		ID3D12Device* device = s_D3D12->GetDevice();
		ID3D12CommandQueue* queue = s_D3D12->GetCommandQueue();

		// Wait on the previous job (example only - simplifies resource management)
		if (s_D3D12Fence->GetCompletedValue() < s_D3D12FenceValue)
		{
			s_D3D12Fence->SetEventOnCompletion(s_D3D12FenceValue, s_D3D12Event);
			WaitForSingleObject(s_D3D12Event, INFINITE);
		}

		// Update native texture from code
		if (mTexturePointer)
		{
			ID3D12Resource* resource = (ID3D12Resource*)mTexturePointer;
			D3D12_RESOURCE_DESC desc = resource->GetDesc();

			// Begin a command list
			s_D3D12CmdAlloc->Reset();
			s_D3D12CmdList->Reset(s_D3D12CmdAlloc, nullptr);

			// Fill data
			const UINT64 kDataSize = desc.Width*desc.Height * 4;
			ID3D12Resource* upload = GetD3D12UploadResource(kDataSize);
			void* mapped = NULL;
			upload->Map(0, NULL, &mapped);
			FillTextureFromCode(desc.Width, desc.Height, desc.Width * 4, (unsigned char*)mapped);
			upload->Unmap(0, NULL);

			D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
			srcLoc.pResource = upload;
			srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			device->GetCopyableFootprints(&desc, 0, 1, 0, &srcLoc.PlacedFootprint, nullptr, nullptr, nullptr);

			D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
			dstLoc.pResource = resource;
			dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dstLoc.SubresourceIndex = 0;

			// Queue data upload
			const D3D12_RESOURCE_STATES kDesiredState = D3D12_RESOURCE_STATE_COPY_DEST;
			D3D12_RESOURCE_STATES resourceState = kDesiredState;
			s_D3D12->GetResourceState(resource, &resourceState); // Get resource state as it will be after all command lists Unity queued before this plugin call execute.
			if (resourceState != kDesiredState)
			{
				D3D12_RESOURCE_BARRIER barrierDesc = {};
				barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrierDesc.Transition.pResource = resource;
				barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrierDesc.Transition.StateBefore = resourceState;
				barrierDesc.Transition.StateAfter = kDesiredState;
				s_D3D12CmdList->ResourceBarrier(1, &barrierDesc);
				s_D3D12->SetResourceState(resource, kDesiredState); // Set resource state as it will be after this command list is executed.
			}

			s_D3D12CmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

			// Execute the command list
			s_D3D12CmdList->Close();
			ID3D12CommandList* lists[1] = { s_D3D12CmdList };
			queue->ExecuteCommandLists(1, lists);
		}

		// Insert fence
		++s_D3D12FenceValue;
		queue->Signal(s_D3D12Fence, s_D3D12FenceValue);
	}
#endif

#if SUPPORT_OPENGL_LEGACY
	// OpenGL 2 legacy case (deprecated)
	if (mUnityPlugin.deviceType() == kUnityGfxRendererOpenGL)
	{
		// update native texture from code
		if (mTexturePointer)
		{
			GLuint gltex = (GLuint)(size_t)(mTexturePointer);
			glBindTexture(GL_TEXTURE_2D, gltex);
			//int texWidth, texHeight;
			//glGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texWidth);
			//glGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texHeight);

			//unsigned char* data = new unsigned char[texWidth*texHeight*4];
			//FillTextureFromCode (texWidth, texHeight, texWidth*4, data);
			//glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, texWidth, texHeight, GL_RGBA, GL_UNSIGNED_BYTE, data);
			//delete[] data;

			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, buffer->width(), buffer->height(), GL_RGBA, GL_UNSIGNED_BYTE, buffer->cst_data());
		}
	}
#endif

#if SUPPORT_OPENGL_UNIFIED
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

	// OpenGL ES / core case
	if (s_DeviceType == kUnityGfxRendererOpenGLES20 ||
		s_DeviceType == kUnityGfxRendererOpenGLES30 ||
		s_DeviceType == kUnityGfxRendererOpenGLCore)
	{
		assert(glGetError() == GL_NO_ERROR); // Make sure no OpenGL error happen before starting rendering

											 // update native texture from code
		if (mTexturePointer)
		{
			GLuint gltex = (GLuint)(size_t)(mTexturePointer);
			glBindTexture(GL_TEXTURE_2D, gltex);
			// The script only pass width and height with OpenGL ES on mobile
#if SUPPORT_OPENGL_CORE
			if (s_DeviceType == kUnityGfxRendererOpenGLCore)
			{
				glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mWidth);
				glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &mHeight);
			}
#endif

			unsigned char* data = new unsigned char[mWidth*mHeight * 4];
			FillTextureFromCode(mWidth, mHeight, mWidth * 4, data);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mWidth, mHeight, GL_RGBA, GL_UNSIGNED_BYTE, data);
			delete[] data;
		}

		assert(glGetError() == GL_NO_ERROR);
	}
#endif
}

void UnityTexture::FillBuffer(unsigned char * dst, const FrameBuffer *src, int width, int height, int stride)
{
	if (!src || !dst)
		return;

	if (src->width() == width && src->height() == height)
	{
		const uint8_t *ptrSrc = src->cst_data();

		for (int y = 0; y < height; ++y)
		{
			uint8_t *ptrDst = dst;
			for (int x = 0; x < width; ++x)
			{
				// Simple oldskool "plasma effect", a bunch of combined sine waves

				// Write the texture pixel
				ptrDst[0] = ptrSrc[0];
				ptrDst[1] = ptrSrc[1];
				ptrDst[2] = ptrSrc[2];
				ptrDst[3] = ptrSrc[3];

				// To next pixel (our pixels are 4 bpp)
				ptrDst += 4;
				ptrSrc += 4;
			}

			// To next image row
			dst += stride;
		}
	}
}