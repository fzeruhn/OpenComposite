#include "stdafx.h"
#define BASE_IMPL
#include "BaseIOBuffer.h"

OOVR_EIOBufferError BaseIOBuffer::Open(const char* pchPath, OOVR_EIOBufferMode mode, uint32_t unElementSize, uint32_t unElements,
    OOVR_IOBufferHandle_t* pulBuffer)
{
	if (pulBuffer)
		*pulBuffer = 0; // k_ulInvalidIOBufferHandle
	return OOVR_IOBuffer_InvalidArgument;
}

OOVR_EIOBufferError BaseIOBuffer::Close(OOVR_IOBufferHandle_t ulBuffer)
{
	return OOVR_IOBuffer_InvalidHandle;
}

OOVR_EIOBufferError BaseIOBuffer::Read(OOVR_IOBufferHandle_t ulBuffer, void* pDst, uint32_t unBytes, uint32_t* punRead)
{
	if (punRead)
		*punRead = 0;
	return OOVR_IOBuffer_InvalidHandle;
}

OOVR_EIOBufferError BaseIOBuffer::Write(OOVR_IOBufferHandle_t ulBuffer, void* pSrc, uint32_t unBytes)
{
	return OOVR_IOBuffer_InvalidHandle;
}

vr::PropertyContainerHandle_t BaseIOBuffer::PropertyContainer(OOVR_IOBufferHandle_t ulBuffer)
{
	return vr::k_ulInvalidPropertyContainer;
}

bool BaseIOBuffer::HasReaders(OOVR_IOBufferHandle_t ulBuffer)
{
	return false;
}
