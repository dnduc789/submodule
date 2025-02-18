/*!
 * \copy
 *     Copyright (c)  2009-2013, Cisco Systems
 *     All rights reserved.
 *
 *     Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 *     are met:
 *
 *        * Redistributions of source code must retain the above copyright
 *          notice, this list of conditions and the following disclaimer.
 *
 *        * Redistributions in binary form must reproduce the above copyright
 *          notice, this list of conditions and the following disclaimer in
 *          the documentation and/or other materials provided with the
 *          distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *     "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *     COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *     BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *     CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *     ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *     POSSIBILITY OF SUCH DAMAGE.
 *
 *
 *  welsDecoderExt.cpp
 *
 *  Abstract
 *      Cisco OpenH264 decoder extension utilization
 *
 *  History
 *      3/12/2009 Created
 *
 *
 ************************************************************************/
//#include <assert.h>
#include "welsDecoderExt.h"
#include "welsCodecTrace.h"
#include "codec_def.h"
#include "typedefs.h"
#include "mem_align.h"
#include "utils.h"
#include "version.h"

//#include "macros.h"
#include "decoder.h"
#include "decoder_core.h"
#include "error_concealment.h"

#include "measure_time.h"
extern "C" {
#include "decoder_core.h"
#include "manage_dec_ref.h"
}
#include "error_code.h"
#include "crt_util_safe_x.h"	// Safe CRT routines like util for cross platforms
#include <time.h>
#if defined(_WIN32) /*&& defined(_DEBUG)*/

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#else
#include <sys/time.h>
#endif

namespace WelsDec {

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

/***************************************************************************
*	Description:
*			class CWelsDecoder constructor function, do initialization	and
*       alloc memory required
*
*	Input parameters: none
*
*	return: none
***************************************************************************/
CWelsDecoder::CWelsDecoder (void)
  :	m_pDecContext (NULL),
    m_pWelsTrace (NULL) {
#ifdef OUTPUT_BIT_STREAM
  char chFileName[1024] = { 0 };  //for .264
  int iBufUsed = 0;
  int iBufLeft = 1023;
  int iCurUsed;

  char chFileNameSize[1024] = { 0 }; //for .len
  int iBufUsedSize = 0;
  int iBufLeftSize = 1023;
  int iCurUsedSize;
#endif//OUTPUT_BIT_STREAM


  m_pWelsTrace	= new welsCodecTrace();
  if (m_pWelsTrace != NULL) {
    m_pWelsTrace->SetTraceLevel (WELS_LOG_ERROR);

    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO, "CWelsDecoder::CWelsDecoder() entry");
  }

#ifdef OUTPUT_BIT_STREAM
  SWelsTime sCurTime;

  WelsGetTimeOfDay (&sCurTime);

  iCurUsed     = WelsSnprintf (chFileName,  iBufLeft,  "bs_0x%p_", (void*)this);
  iCurUsedSize = WelsSnprintf (chFileNameSize, iBufLeftSize, "size_0x%p_", (void*)this);

  iBufUsed += iCurUsed;
  iBufLeft -= iCurUsed;
  if (iBufLeft > 0) {
    iCurUsed = WelsStrftime (&chFileName[iBufUsed], iBufLeft, "%y%m%d%H%M%S", &sCurTime);
    iBufUsed += iCurUsed;
    iBufLeft -= iCurUsed;
  }

  iBufUsedSize += iCurUsedSize;
  iBufLeftSize -= iCurUsedSize;
  if (iBufLeftSize > 0) {
    iCurUsedSize = WelsStrftime (&chFileNameSize[iBufUsedSize], iBufLeftSize, "%y%m%d%H%M%S", &sCurTime);
    iBufUsedSize += iCurUsedSize;
    iBufLeftSize -= iCurUsedSize;
  }

  if (iBufLeft > 0) {
    iCurUsed = WelsSnprintf (&chFileName[iBufUsed], iBufLeft, ".%03.3u.264", WelsGetMillisecond (&sCurTime));
    iBufUsed += iCurUsed;
    iBufLeft -= iCurUsed;
  }

  if (iBufLeftSize > 0) {
    iCurUsedSize = WelsSnprintf (&chFileNameSize[iBufUsedSize], iBufLeftSize, ".%03.3u.len",
                                 WelsGetMillisecond (&sCurTime));
    iBufUsedSize += iCurUsedSize;
    iBufLeftSize -= iCurUsedSize;
  }


  m_pFBS = WelsFopen (chFileName, "wb");
  m_pFBSSize = WelsFopen (chFileNameSize, "wb");
#endif//OUTPUT_BIT_STREAM

}

/***************************************************************************
*	Description:
*			class CWelsDecoder destructor function, destroy allocced memory
*
*	Input parameters: none
*
*	return: none
***************************************************************************/
CWelsDecoder::~CWelsDecoder() {
  if (m_pWelsTrace != NULL) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO, "CWelsDecoder::~CWelsDecoder()");
  }

  UninitDecoder();

#ifdef OUTPUT_BIT_STREAM
  if (m_pFBS) {
    WelsFclose (m_pFBS);
    m_pFBS = NULL;
  }
  if (m_pFBSSize) {
    WelsFclose (m_pFBSSize);
    m_pFBSSize = NULL;
  }
#endif//OUTPUT_BIT_STREAM

  if (m_pWelsTrace != NULL) {
    delete m_pWelsTrace;
    m_pWelsTrace = NULL;
  }
}

long CWelsDecoder::Initialize (const SDecodingParam* pParam) {
  int iRet = ERR_NONE;
  if (m_pWelsTrace == NULL) {
    return cmMallocMemeError;
  }

  if (pParam == NULL) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_ERROR, "CWelsDecoder::Initialize(), invalid input argument.");
    return cmInitParaError;
  }

  // H.264 decoder initialization,including memory allocation,then open it ready to decode
  iRet = InitDecoder();
  if (iRet)
    return iRet;

  iRet = DecoderConfigParam (m_pDecContext, pParam);
  if (iRet)
    return iRet;

  return cmResultSuccess;
}

long CWelsDecoder::Uninitialize() {
  UninitDecoder();

  return ERR_NONE;
}

void CWelsDecoder::UninitDecoder (void) {
  if (NULL == m_pDecContext)
    return;

  WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO, "CWelsDecoder::uninit_decoder(), openh264 codec version = %s.",
           VERSION_NUMBER);

  WelsEndDecoder (m_pDecContext);

  if (NULL != m_pDecContext) {
    WelsFree (m_pDecContext, "m_pDecContext");

    m_pDecContext	= NULL;
  }

}

// the return value of this function is not suitable, it need report failure info to upper layer.
int32_t CWelsDecoder::InitDecoder (void) {

  WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO, "CWelsDecoder::init_decoder(), openh264 codec version = %s",
           VERSION_NUMBER);

  m_pDecContext	= (PWelsDecoderContext)WelsMalloc (sizeof (SWelsDecoderContext), "m_pDecContext");
  if (NULL == m_pDecContext)
    return cmMallocMemeError;

  return WelsInitDecoder (m_pDecContext, &m_pWelsTrace->m_sLogCtx);
}

/*
 * Set Option
 */
long CWelsDecoder::SetOption (DECODER_OPTION eOptID, void* pOption) {
  int iVal = 0;

  if (m_pDecContext == NULL && eOptID != DECODER_OPTION_TRACE_LEVEL &&
      eOptID != DECODER_OPTION_TRACE_CALLBACK && eOptID != DECODER_OPTION_TRACE_CALLBACK_CONTEXT)
    return dsInitialOptExpected;

  if (eOptID == DECODER_OPTION_DATAFORMAT) { // Set color space of decoding output frame
    if (pOption == NULL)
      return cmInitParaError;

    iVal = * ((int*)pOption);	// is_rgb

    return DecoderSetCsp (m_pDecContext, iVal);
  } else if (eOptID == DECODER_OPTION_END_OF_STREAM) { // Indicate bit-stream of the final frame to be decoded
    if (pOption == NULL)
      return cmInitParaError;

    iVal	= * ((int*)pOption);	// boolean value for whether enabled End Of Stream flag

    m_pDecContext->bEndOfStreamFlag	= iVal ? true : false;

    return cmResultSuccess;
  } else if (eOptID == DECODER_OPTION_ERROR_CON_IDC) { // Indicate error concealment status
    if (pOption == NULL)
      return cmInitParaError;

    iVal	= * ((int*)pOption);	// int value for error concealment idc
    iVal = WELS_CLIP3 (iVal, (int32_t) ERROR_CON_DISABLE, (int32_t) ERROR_CON_SLICE_COPY_CROSS_IDR_FREEZE_RES_CHANGE);
    m_pDecContext->eErrorConMethod = (ERROR_CON_IDC) iVal;
    InitErrorCon (m_pDecContext);
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO,
             "CWelsDecoder::SetOption for ERROR_CON_IDC = %d.", iVal);

    return cmResultSuccess;
  } else if (eOptID == DECODER_OPTION_TRACE_LEVEL) {
    if (m_pWelsTrace) {
      uint32_t level = * ((uint32_t*)pOption);
      m_pWelsTrace->SetTraceLevel (level);
    }
    return cmResultSuccess;
  } else if (eOptID == DECODER_OPTION_TRACE_CALLBACK) {
    if (m_pWelsTrace) {
      WelsTraceCallback callback = * ((WelsTraceCallback*)pOption);
      m_pWelsTrace->SetTraceCallback (callback);
      WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO, "CWelsDecoder::SetOption(), openh264 codec version = %s.",
               VERSION_NUMBER);
    }
    return cmResultSuccess;
  } else if (eOptID == DECODER_OPTION_TRACE_CALLBACK_CONTEXT) {
    if (m_pWelsTrace) {
      void* ctx = * ((void**)pOption);
      m_pWelsTrace->SetTraceCallbackContext (ctx);
    }
    return cmResultSuccess;
  } else if (eOptID == DECODER_OPTION_GET_STATISTICS) {
    WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_WARNING,
             "CWelsDecoder::SetOption():DECODER_OPTION_GET_STATISTICS: this option is get-only!");
    return cmInitParaError;
  }


  return cmInitParaError;
}

/*
 *	Get Option
 */
long CWelsDecoder::GetOption (DECODER_OPTION eOptID, void* pOption) {
  int iVal = 0;

  if (m_pDecContext == NULL)
    return cmInitExpected;

  if (pOption == NULL)
    return cmInitParaError;

  if (DECODER_OPTION_DATAFORMAT == eOptID) {
    iVal = (int32_t) m_pDecContext->eOutputColorFormat;
    * ((int*)pOption)	= iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_END_OF_STREAM == eOptID) {
    iVal	= m_pDecContext->bEndOfStreamFlag;
    * ((int*)pOption)	= iVal;
    return cmResultSuccess;
  }
#ifdef LONG_TERM_REF
  else if (DECODER_OPTION_IDR_PIC_ID == eOptID) {
    iVal = m_pDecContext->uiCurIdrPicId;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_FRAME_NUM == eOptID) {
    iVal = m_pDecContext->iFrameNum;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_LTR_MARKING_FLAG == eOptID) {
    iVal = m_pDecContext->bCurAuContainLtrMarkSeFlag;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_LTR_MARKED_FRAME_NUM == eOptID) {
    iVal = m_pDecContext->iFrameNumOfAuMarkedLtr;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  }
#endif
  else if (DECODER_OPTION_VCL_NAL == eOptID) { //feedback whether or not have VCL NAL in current AU
    iVal = m_pDecContext->iFeedbackVclNalInAu;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_TEMPORAL_ID == eOptID) { //if have VCL NAL in current AU, then feedback the temporal ID
    iVal = m_pDecContext->iFeedbackTidInAu;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_ERROR_CON_IDC == eOptID) {
    iVal = (int) m_pDecContext->eErrorConMethod;
    * ((int*)pOption) = iVal;
    return cmResultSuccess;
  } else if (DECODER_OPTION_GET_STATISTICS == eOptID) { // get decoder statistics info for real time debugging
    SDecoderStatistics* pDecoderStatistics = (static_cast<SDecoderStatistics*> (pOption));

    memcpy (pDecoderStatistics, &m_pDecContext->sDecoderStatistics, sizeof (SDecoderStatistics));

    pDecoderStatistics->fAverageFrameSpeedInMs = (float) (m_pDecContext->dDecTime) /
        (m_pDecContext->sDecoderStatistics.uiDecodedFrameCount);
    memset (&m_pDecContext->sDecoderStatistics, 0, sizeof (SDecoderStatistics));
    m_pDecContext->dDecTime = 0;
    return cmResultSuccess;
  }

  return cmInitParaError;
}

DECODING_STATE CWelsDecoder::DecodeFrame2 (const unsigned char* kpSrc,
    const int kiSrcLen,
    unsigned char** ppDst,
    SBufferInfo* pDstInfo) {
  if (CheckBsBuffer (m_pDecContext, kiSrcLen)) {
    return dsOutOfMemory;
  }
  if (kiSrcLen > 0 && kpSrc != NULL) {
#ifdef OUTPUT_BIT_STREAM
    if (m_pFBS) {
      WelsFwrite (kpSrc, sizeof (unsigned char), kiSrcLen, m_pFBS);
      WelsFflush (m_pFBS);
    }
    if (m_pFBSSize) {
      WelsFwrite (&kiSrcLen, sizeof (int), 1, m_pFBSSize);
      WelsFflush (m_pFBSSize);
    }
#endif//OUTPUT_BIT_STREAM
    m_pDecContext->bEndOfStreamFlag = false;
  } else {
    //For application MODE, the error detection should be added for safe.
    //But for CONSOLE MODE, when decoding LAST AU, kiSrcLen==0 && kpSrc==NULL.
    m_pDecContext->bEndOfStreamFlag = true;
    m_pDecContext->bInstantDecFlag = true;
  }

  int64_t iStart, iEnd;
  iStart = WelsTime();
  ppDst[0] = ppDst[1] = ppDst[2] = NULL;
  m_pDecContext->iErrorCode             = dsErrorFree; //initialize at the starting of AU decoding.
  m_pDecContext->iFeedbackVclNalInAu = FEEDBACK_UNKNOWN_NAL; //initialize
  memset (pDstInfo, 0, sizeof (SBufferInfo));

#ifdef LONG_TERM_REF
  m_pDecContext->bReferenceLostAtT0Flag       = false; //initialize for LTR
  m_pDecContext->bCurAuContainLtrMarkSeFlag = false;
  m_pDecContext->iFrameNumOfAuMarkedLtr      = 0;
  m_pDecContext->iFrameNum                       = -1; //initialize
#endif

  m_pDecContext->iFeedbackTidInAu             = -1; //initialize

  WelsDecodeBs (m_pDecContext, kpSrc, kiSrcLen, ppDst,
                pDstInfo); //iErrorCode has been modified in this function
  m_pDecContext->bInstantDecFlag = false; //reset no-delay flag
  if (m_pDecContext->iErrorCode) {
    EWelsNalUnitType eNalType =
      NAL_UNIT_UNSPEC_0;	//for NBR, IDR frames are expected to decode as followed if error decoding an IDR currently

    eNalType	= m_pDecContext->sCurNalHead.eNalUnitType;

    //for AVC bitstream (excluding AVC with temporal scalability, including TP), as long as error occur, SHOULD notify upper layer key frame loss.
    if ((IS_PARAM_SETS_NALS (eNalType) || NAL_UNIT_CODED_SLICE_IDR == eNalType) ||
        (VIDEO_BITSTREAM_AVC == m_pDecContext->eVideoType)) {
      if (m_pDecContext->eErrorConMethod == ERROR_CON_DISABLE) {
#ifdef LONG_TERM_REF
        m_pDecContext->bParamSetsLostFlag = true;
#else
        m_pDecContext->bReferenceLostAtT0Flag = true;
#endif
        ResetParameterSetsState (m_pDecContext);  //initial SPS&PPS ready flag
      }
    }

    if (m_pDecContext->bPrintFrameErrorTraceFlag) {
      WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_INFO, "decode failed, failure type:%d \n",
               m_pDecContext->iErrorCode);
      m_pDecContext->bPrintFrameErrorTraceFlag = false;
    } else {
      m_pDecContext->iIgnoredErrorInfoPacketCount ++;
      if (m_pDecContext->iIgnoredErrorInfoPacketCount == INT_MAX) {
        WelsLog (&m_pWelsTrace->m_sLogCtx, WELS_LOG_WARNING, "continuous error reached INT_MAX! Restart as 0.");
        m_pDecContext->iIgnoredErrorInfoPacketCount = 0;
      }
    }
    if ((m_pDecContext->eErrorConMethod != ERROR_CON_DISABLE) && (pDstInfo->iBufferStatus == 1)) {
      //TODO after dec status updated
      m_pDecContext->iErrorCode |= dsDataErrorConcealed;
      if (m_pDecContext->eErrorConMethod == ERROR_CON_FRAME_COPY)

        m_pDecContext->sDecoderStatistics.uiAvgEcRatio = (m_pDecContext->sDecoderStatistics.uiAvgEcRatio *
            m_pDecContext->sDecoderStatistics.uiEcFrameNum) + 100;

      //
      if ((m_pDecContext->sDecoderStatistics.uiWidth != (unsigned int) pDstInfo->UsrData.sSystemBuffer.iWidth)
          || (m_pDecContext->sDecoderStatistics.uiHeight != (unsigned int) pDstInfo->UsrData.sSystemBuffer.iHeight)) {
        m_pDecContext->sDecoderStatistics.uiResolutionChangeTimes++;
        m_pDecContext->sDecoderStatistics.uiWidth = pDstInfo->UsrData.sSystemBuffer.iWidth;
        m_pDecContext->sDecoderStatistics.uiHeight = pDstInfo->UsrData.sSystemBuffer.iHeight;

      }
      m_pDecContext->sDecoderStatistics.uiDecodedFrameCount++;
      m_pDecContext->sDecoderStatistics.uiEcFrameNum++;
      m_pDecContext->sDecoderStatistics.uiAvgEcRatio = m_pDecContext->sDecoderStatistics.uiAvgEcRatio /
          m_pDecContext->sDecoderStatistics.uiEcFrameNum;
    }
    iEnd = WelsTime();
    m_pDecContext->dDecTime += (iEnd - iStart) / 1e3;
    return (DECODING_STATE) m_pDecContext->iErrorCode;
  }
  // else Error free, the current codec works well

  if (pDstInfo->iBufferStatus == 1) {

    if ((m_pDecContext->sDecoderStatistics.uiWidth != (unsigned int) pDstInfo->UsrData.sSystemBuffer.iWidth)
        || (m_pDecContext->sDecoderStatistics.uiHeight != (unsigned int) pDstInfo->UsrData.sSystemBuffer.iHeight)) {
      m_pDecContext->sDecoderStatistics.uiResolutionChangeTimes++;
      m_pDecContext->sDecoderStatistics.uiWidth = pDstInfo->UsrData.sSystemBuffer.iWidth;
      m_pDecContext->sDecoderStatistics.uiHeight = pDstInfo->UsrData.sSystemBuffer.iHeight;

    }
    m_pDecContext->sDecoderStatistics.uiDecodedFrameCount++;
  }
  iEnd = WelsTime();
  m_pDecContext->dDecTime += (iEnd - iStart) / 1e3;
  return dsErrorFree;
}

DECODING_STATE CWelsDecoder::DecodeParser (const unsigned char* kpSrc,
    const int kiSrcLen,
    SParserBsInfo* pDstInfo) {
//TODO, add function here
  return (DECODING_STATE) m_pDecContext->iErrorCode;
}

DECODING_STATE CWelsDecoder::DecodeFrame (const unsigned char* kpSrc,
    const int kiSrcLen,
    unsigned char** ppDst,
    int* pStride,
    int& iWidth,
    int& iHeight) {
  DECODING_STATE eDecState = dsErrorFree;
  SBufferInfo    DstInfo;

  memset (&DstInfo, 0, sizeof (SBufferInfo));
  DstInfo.UsrData.sSystemBuffer.iStride[0] = pStride[0];
  DstInfo.UsrData.sSystemBuffer.iStride[1] = pStride[1];
  DstInfo.UsrData.sSystemBuffer.iWidth = iWidth;
  DstInfo.UsrData.sSystemBuffer.iHeight = iHeight;

  eDecState = DecodeFrame2 (kpSrc, kiSrcLen, ppDst, &DstInfo);
  if (eDecState == dsErrorFree) {
    pStride[0] = DstInfo.UsrData.sSystemBuffer.iStride[0];
    pStride[1] = DstInfo.UsrData.sSystemBuffer.iStride[1];
    iWidth     = DstInfo.UsrData.sSystemBuffer.iWidth;
    iHeight    = DstInfo.UsrData.sSystemBuffer.iHeight;
  }

  return eDecState;
}

DECODING_STATE CWelsDecoder::DecodeFrameEx (const unsigned char* kpSrc,
    const int kiSrcLen,
    unsigned char* pDst,
    int iDstStride,
    int& iDstLen,
    int& iWidth,
    int& iHeight,
    int& iColorFormat) {
  DECODING_STATE	 state = dsErrorFree;

  return state;
}


} // namespace WelsDec


using namespace WelsDec;
/*
*       WelsGetDecoderCapability
*       @return: DecCapability information
*/
int WelsGetDecoderCapability (SDecoderCapability* pDecCapability) {
  memset (pDecCapability, 0, sizeof (SDecoderCapability));
  pDecCapability->iProfileIdc = 66; //Baseline
  pDecCapability->iProfileIop = 0xE0; //11100000b
  pDecCapability->iLevelIdc = 32; //level_idc = 3.2
  pDecCapability->iMaxMbps = 216000; //from level_idc = 3.2
  pDecCapability->iMaxFs = 5120; //from level_idc = 3.2
  pDecCapability->iMaxCpb = 20000; //from level_idc = 3.2
  pDecCapability->iMaxDpb = 20480; //from level_idc = 3.2
  pDecCapability->iMaxBr = 20000; //from level_idc = 3.2
  pDecCapability->bRedPicCap = 0; //not support redundant pic

  return ERR_NONE;
}
/* WINAPI is indeed in prefix due to sync to application layer callings!! */

/*
*	WelsCreateDecoder
*	@return:	success in return 0, otherwise failed.
*/
long WelsCreateDecoder (ISVCDecoder** ppDecoder) {

  if (NULL == ppDecoder) {
    return ERR_INVALID_PARAMETERS;
  }

  *ppDecoder	= new CWelsDecoder();

  if (NULL == *ppDecoder) {
    return ERR_MALLOC_FAILED;
  }

  return ERR_NONE;
}

/*
*	WelsDestroyDecoder
*/
void WelsDestroyDecoder (ISVCDecoder* pDecoder) {
  if (NULL != pDecoder) {
    delete (CWelsDecoder*)pDecoder;
  }
}
