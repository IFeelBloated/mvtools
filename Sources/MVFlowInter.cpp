// Pixels flow motion interplolation function
// Copyright(c)2005-2006 A.G.Balakhnin aka Fizick

// See legal notice in Copying.txt for more information

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .

#include "ClipFnc.h"
#include "MVFlowInter.h"
#include "MaskFun.h"
#include "MVFinest.h"
#include "SuperParams64Bits.h"
#include "Time256ProviderCst.h"
#include "Time256ProviderPlane.h"

MVFlowInter::MVFlowInter(PClip _child, PClip super, PClip _mvbw, PClip _mvfw,  int _time256, double _ml,
                           bool _blend, int nSCD1, int nSCD2, bool _isse, bool _planar, PClip _timeclip, IScriptEnvironment* env) :
GenericVideoFilter(_child),
MVFilter(_mvfw, "MFlowInter", env, 1, 0),
mvClipB(_mvbw, nSCD1, nSCD2, env, 1, 0),
mvClipF(_mvfw, nSCD1, nSCD2, env, 1, 0),
timeclip (_timeclip)
{
	if (_timeclip != 0)
	{
		const ::VideoInfo &	vi_tst = timeclip->GetVideoInfo ();
		const bool		same_format_flag = (   vi_tst.height     == vi.height
							                    && vi_tst.width      == vi.width
							                    && vi_tst.num_frames == vi.num_frames
							                    && vi_tst.IsSameColorspace (vi));
		if (! same_format_flag)
		{
			env->ThrowError("MFlow: tclip format is different from the main clip.");
		}
	}

	time256 = _time256;
	ml = _ml;
	isse = _isse;
	planar = _planar;
	blend = _blend;

	CheckSimilarity(mvClipB, "mvbw", env);
	CheckSimilarity(mvClipF, "mvfw", env);

	SuperParams64Bits params;
	memcpy(&params, &super->GetVideoInfo().num_audio_samples, 8);
	int nHeightS = params.nHeight;
	int nSuperHPad = params.nHPad;
	int nSuperVPad = params.nVPad;
	int nSuperPel = params.nPel;
	int nSuperModeYUV = params.nModeYUV;
	int nSuperLevels = params.nLevels;
	int nSuperWidth = super->GetVideoInfo().width; // really super
	int nSuperHeight = super->GetVideoInfo().height;

	if (   nHeight != nHeightS
	    || nWidth  != nSuperWidth - nSuperHPad * 2
	    || nPel    != nSuperPel)
	{
		env->ThrowError("MFlowInter : wrong super frame clip");
	}

	if (nPel==1)
		finest = super; // v2.0.9.1
	else
	{
		finest = new MVFinest(super, isse, env);
		AVSValue cache_args[1] = { finest };
		finest = env->Invoke("InternalCache", AVSValue(cache_args,1)).AsClip(); // add cache for speed
	}

//	if (nWidth  != vi.width || (nWidth + nHPadding*2)*nPel != finest->GetVideoInfo().width ||
//	    nHeight  != vi.height || (nHeight + nVPadding*2)*nPel != finest->GetVideoInfo().height )
//		env->ThrowError("MVFlowInter: wrong source or finest frame size");

	// may be padded for full frame cover
	nBlkXP = (nBlkX*(nBlkSizeX - nOverlapX) + nOverlapX < nWidth) ? nBlkX+1 : nBlkX;
	nBlkYP = (nBlkY*(nBlkSizeY - nOverlapY) + nOverlapY < nHeight) ? nBlkY+1 : nBlkY;
	nWidthP = nBlkXP*(nBlkSizeX - nOverlapX) + nOverlapX;
	nHeightP = nBlkYP*(nBlkSizeY - nOverlapY) + nOverlapY;
	// for YV12
	nWidthPUV = nWidthP/2;
	nHeightPUV = nHeightP/yRatioUV;
	nHeightUV = nHeight/yRatioUV;
	nWidthUV = nWidth/2;

	nHPaddingUV = nHPadding/2;
	nVPaddingUV = nVPadding/yRatioUV;

	VPitchY = (nWidthP + 15) & (~15);
	VPitchUV = (nWidthPUV + 15) & (~15);

	VXFullYB = new BYTE [nHeightP*VPitchY];
	VXFullUVB = new BYTE [nHeightPUV*VPitchUV];
	VYFullYB = new BYTE [nHeightP*VPitchY];
	VYFullUVB = new BYTE [nHeightPUV*VPitchUV];

	VXFullYF = new BYTE [nHeightP*VPitchY];
	VXFullUVF = new BYTE [nHeightPUV*VPitchUV];
	VYFullYF = new BYTE [nHeightP*VPitchY];
	VYFullUVF = new BYTE [nHeightPUV*VPitchUV];

	VXSmallYB = new BYTE [nBlkXP*nBlkYP];
	VYSmallYB = new BYTE [nBlkXP*nBlkYP];
	VXSmallUVB = new BYTE [nBlkXP*nBlkYP];
	VYSmallUVB = new BYTE [nBlkXP*nBlkYP];

	VXSmallYF = new BYTE [nBlkXP*nBlkYP];
	VYSmallYF = new BYTE [nBlkXP*nBlkYP];
	VXSmallUVF = new BYTE [nBlkXP*nBlkYP];
	VYSmallUVF = new BYTE [nBlkXP*nBlkYP];

	VXFullYBB = new BYTE [nHeightP*VPitchY];
	VXFullUVBB = new BYTE [nHeightPUV*VPitchUV];
	VYFullYBB = new BYTE [nHeightP*VPitchY];
	VYFullUVBB = new BYTE [nHeightPUV*VPitchUV];

	VXFullYFF = new BYTE [nHeightP*VPitchY];
	VXFullUVFF = new BYTE [nHeightPUV*VPitchUV];
	VYFullYFF = new BYTE [nHeightP*VPitchY];
	VYFullUVFF = new BYTE [nHeightPUV*VPitchUV];

	VXSmallYBB = new BYTE [nBlkXP*nBlkYP];
	VYSmallYBB = new BYTE [nBlkXP*nBlkYP];
	VXSmallUVBB = new BYTE [nBlkXP*nBlkYP];
	VYSmallUVBB = new BYTE [nBlkXP*nBlkYP];

	VXSmallYFF = new BYTE [nBlkXP*nBlkYP];
	VYSmallYFF = new BYTE [nBlkXP*nBlkYP];
	VXSmallUVFF = new BYTE [nBlkXP*nBlkYP];
	VYSmallUVFF = new BYTE [nBlkXP*nBlkYP];

	MaskSmallB = new BYTE [nBlkXP*nBlkYP];
	MaskFullYB = new BYTE [nHeightP*VPitchY];
	MaskFullUVB = new BYTE [nHeightPUV*VPitchUV];

	MaskSmallF = new BYTE [nBlkXP*nBlkYP];
	MaskFullYF = new BYTE [nHeightP*VPitchY];
	MaskFullUVF = new BYTE [nHeightPUV*VPitchUV];

	SADMaskSmallB = new BYTE [nBlkXP*nBlkYP];
	SADMaskSmallF = new BYTE [nBlkXP*nBlkYP];


	int CPUF_Resize = env->GetCPUFlags();
	if (!isse) CPUF_Resize = (CPUF_Resize & !CPUF_INTEGER_SSE) & !CPUF_SSE2;

	upsizer = new SimpleResize(nWidthP, nHeightP, nBlkXP, nBlkYP, CPUF_Resize);
	upsizerUV = new SimpleResize(nWidthPUV, nHeightPUV, nBlkXP, nBlkYP, CPUF_Resize);

	if (timeclip == 0)
	{
		LUTVB = new VectLut [1];
		LUTVF = new VectLut [1];
		Create_LUTV(time256, LUTVB [0], LUTVF [0]);
	}
	else
	{
		LUTVB = new VectLut [256];
		LUTVF = new VectLut [256];
		for (int t256 = 0; t256 < 256; ++t256)
		{
			Create_LUTV(t256, LUTVB [t256], LUTVF [t256]);
		}
	}

	if ( (pixelType & VideoInfo::CS_YUY2) == VideoInfo::CS_YUY2 && !planar)
	{
		DstPlanes =  new YUY2Planes(nWidth, nHeight);
	}

}

MVFlowInter::~MVFlowInter()
{
	if ( (pixelType & VideoInfo::CS_YUY2) == VideoInfo::CS_YUY2  && !planar)
	{
		delete DstPlanes;
	}

	delete upsizer;
	delete upsizerUV;

	delete [] VXFullYB;
	delete [] VXFullUVB;
	delete [] VYFullYB;
	delete [] VYFullUVB;
	delete [] VXSmallYB;
	delete [] VYSmallYB;
	delete [] VXSmallUVB;
	delete [] VYSmallUVB;
	delete [] VXFullYF;
	delete [] VXFullUVF;
	delete [] VYFullYF;
	delete [] VYFullUVF;
	delete [] VXSmallYF;
	delete [] VYSmallYF;
	delete [] VXSmallUVF;
	delete [] VYSmallUVF;

	delete [] VXFullYBB;
	delete [] VXFullUVBB;
	delete [] VYFullYBB;
	delete [] VYFullUVBB;
	delete [] VXSmallYBB;
	delete [] VYSmallYBB;
	delete [] VXSmallUVBB;
	delete [] VYSmallUVBB;
	delete [] VXFullYFF;
	delete [] VXFullUVFF;
	delete [] VYFullYFF;
	delete [] VYFullUVFF;
	delete [] VXSmallYFF;
	delete [] VYSmallYFF;
	delete [] VXSmallUVFF;
	delete [] VYSmallUVFF;

	delete [] MaskSmallB;
	delete [] MaskFullYB;
	delete [] MaskFullUVB;
	delete [] MaskSmallF;
	delete [] MaskFullYF;
	delete [] MaskFullUVF;

	delete [] SADMaskSmallB;
	delete [] SADMaskSmallF;

	delete [] LUTVB;
	delete [] LUTVF;

}

//-------------------------------------------------------------------------
PVideoFrame __stdcall MVFlowInter::GetFrame(int n, IScriptEnvironment* env)
{
	PVideoFrame dst, t256;
	BYTE *pDst[3];
	const BYTE *pRef[3], *pSrc[3];
	int nDstPitches[3], nRefPitches[3], nSrcPitches[3];
	const BYTE *pt256[3];
	int nt256Pitches[3];
	unsigned char *pDstYUY2;
	int nDstPitchYUY2;

	const int		off  = mvClipB.GetDeltaFrame(); // integer offset of reference frame
	if (off <= 0)
	{
		env->ThrowError ("MFlowInter: cannot use motion vectors with absolute frame references.");
	}
	const int		nref = n + off;

	PVideoFrame mvF = mvClipF.GetFrame(nref, env);
	mvClipF.Update(mvF, env);// forward from current to next
	mvF = 0;
	PVideoFrame mvB = mvClipB.GetFrame(n, env);
	mvClipB.Update(mvB, env);// backward from next to current
	mvB = 0;

	// Checked here instead of the constructor to allow using multi-vector
	// clips, because the backward flag is not reliable before the vector
	// data are actually read from the frame.
	if (!mvClipB.IsBackward())
		env->ThrowError("MFlowInter: wrong backward vectors");
	if (mvClipF.IsBackward())
		env->ThrowError("MFlowInter: wrong forward vectors");

//	int sharp = mvClipB.GetSharp();
	PVideoFrame	src	= finest->GetFrame(n, env);
	PVideoFrame ref = finest->GetFrame(nref, env);//  ref for  compensation
	dst = env->NewVideoFrame(vi);

	const Time256ProviderCst	t256_prov_cst (time256, LUTVB [0], LUTVF [0]);

	if ( mvClipB.IsUsable()  && mvClipF.IsUsable() )
	{
		if (timeclip != 0)
		{
			t256 = timeclip->GetFrame (n, env);
		}

		if ( (pixelType & VideoInfo::CS_YUY2) == VideoInfo::CS_YUY2 )
		{
			// planar data packed to interleaved format (same as interleved2planar by kassandro) - v2.0.0.5
			pSrc[0] = src->GetReadPtr();
			pSrc[1] = pSrc[0] + src->GetRowSize()/2;
			pSrc[2] = pSrc[1] + src->GetRowSize()/4;
			nSrcPitches[0] = src->GetPitch();
			nSrcPitches[1] = nSrcPitches[0];
			nSrcPitches[2] = nSrcPitches[0];
			// planar data packed to interleaved format (same as interleved2planar by kassandro) - v2.0.0.5
			pRef[0] = ref->GetReadPtr();
			pRef[1] = pRef[0] + ref->GetRowSize()/2;
			pRef[2] = pRef[1] + ref->GetRowSize()/4;
			nRefPitches[0] = ref->GetPitch();
			nRefPitches[1] = nRefPitches[0];
			nRefPitches[2] = nRefPitches[0];

			if (!planar)
			{
				pDstYUY2 = dst->GetWritePtr();
				nDstPitchYUY2 = dst->GetPitch();
				pDst[0] = DstPlanes->GetPtr();
				pDst[1] = DstPlanes->GetPtrU();
				pDst[2] = DstPlanes->GetPtrV();
				nDstPitches[0]  = DstPlanes->GetPitch();
				nDstPitches[1]  = DstPlanes->GetPitchUV();
				nDstPitches[2]  = DstPlanes->GetPitchUV();
			}
			else
			{
				pDst[0] = dst->GetWritePtr();
				pDst[1] = pDst[0] + dst->GetRowSize()/2;
				pDst[2] = pDst[1] + dst->GetRowSize()/4;;
				nDstPitches[0] = dst->GetPitch();
				nDstPitches[1] = nDstPitches[0];
				nDstPitches[2] = nDstPitches[0];
			}

			if (timeclip != 0)
			{
				pt256[0] = t256->GetReadPtr();
				pt256[1] = pt256[0] + t256->GetRowSize()/2;
				pt256[2] = pt256[1] + t256->GetRowSize()/4;
				nt256Pitches[0] = t256->GetPitch();
				nt256Pitches[1] = nt256Pitches[0];
				nt256Pitches[2] = nt256Pitches[0];
			}
		}

		else
		{
			pDst[0] = YWPLAN(dst);
			pDst[1] = UWPLAN(dst);
			pDst[2] = VWPLAN(dst);
			nDstPitches[0] = YPITCH(dst);
			nDstPitches[1] = UPITCH(dst);
			nDstPitches[2] = VPITCH(dst);

			pRef[0] = YRPLAN(ref);
			pRef[1] = URPLAN(ref);
			pRef[2] = VRPLAN(ref);
			nRefPitches[0] = YPITCH(ref);
			nRefPitches[1] = UPITCH(ref);
			nRefPitches[2] = VPITCH(ref);

			pSrc[0] = YRPLAN(src);
			pSrc[1] = URPLAN(src);
			pSrc[2] = VRPLAN(src);
			nSrcPitches[0] = YPITCH(src);
			nSrcPitches[1] = UPITCH(src);
			nSrcPitches[2] = VPITCH(src);

			if (timeclip != 0)
			{
				pt256[0] = YRPLAN(t256);
				pt256[1] = URPLAN(t256);
				pt256[2] = VRPLAN(t256);
				nt256Pitches[0] = YPITCH(t256);
				nt256Pitches[1] = UPITCH(t256);
				nt256Pitches[2] = VPITCH(t256);
			}
		}

		int nOffsetY = nRefPitches[0] * nVPadding*nPel + nHPadding*nPel;
		int nOffsetUV = nRefPitches[1] * nVPaddingUV*nPel + nHPaddingUV*nPel;


		// make  vector vx and vy small masks
		// 1. ATTENTION: vectors are assumed SHORT (|vx|, |vy| < 127) !
		// 2. they will be zeroed if not
		// 3. added 128 to all values
		MakeVectorSmallMasks(mvClipB, nBlkX, nBlkY, VXSmallYB, nBlkXP, VYSmallYB, nBlkXP);
		MakeVectorSmallMasks(mvClipF, nBlkX, nBlkY, VXSmallYF, nBlkXP, VYSmallYF, nBlkXP);
		if (nBlkXP > nBlkX) // fill right
		{
			for (int j=0; j<nBlkY; j++)
			{
				VXSmallYB[j*nBlkXP + nBlkX] = std::min(VXSmallYB[j*nBlkXP + nBlkX-1], uint8_t (128));
				VYSmallYB[j*nBlkXP + nBlkX] = VYSmallYB[j*nBlkXP + nBlkX-1];
				VXSmallYF[j*nBlkXP + nBlkX] = std::min(VXSmallYF[j*nBlkXP + nBlkX-1], uint8_t (128));
				VYSmallYF[j*nBlkXP + nBlkX] = VYSmallYF[j*nBlkXP + nBlkX-1];
			}
		}
		if (nBlkYP > nBlkY) // fill bottom
		{
			for (int i=0; i<nBlkXP; i++)
			{
				VXSmallYB[nBlkXP*nBlkY +i] = VXSmallYB[nBlkXP*(nBlkY-1) +i];
				VYSmallYB[nBlkXP*nBlkY +i] = std::min(VYSmallYB[nBlkXP*(nBlkY-1) +i], uint8_t (128));
				VXSmallYF[nBlkXP*nBlkY +i] = VXSmallYF[nBlkXP*(nBlkY-1) +i];
				VYSmallYF[nBlkXP*nBlkY +i] = std::min(VYSmallYF[nBlkXP*(nBlkY-1) +i], uint8_t (128));
			}
		}
		VectorSmallMaskYToHalfUV(VXSmallYB, nBlkXP, nBlkYP, VXSmallUVB, 2);
		VectorSmallMaskYToHalfUV(VYSmallYB, nBlkXP, nBlkYP, VYSmallUVB, yRatioUV);
		VectorSmallMaskYToHalfUV(VXSmallYF, nBlkXP, nBlkYP, VXSmallUVF, 2);
		VectorSmallMaskYToHalfUV(VYSmallYF, nBlkXP, nBlkYP, VYSmallUVF, yRatioUV);

		// analyse vectors field to detect occlusion
		if (timeclip == 0)
		{
			//	  double occNormB = (256-time256)/(256*ml);
			MakeVectorOcclusionMaskTime(mvClipB, nBlkX, nBlkY, ml, 1.0,
				nPel, MaskSmallB, nBlkXP, (256-time256),
				nBlkSizeX - nOverlapX, nBlkSizeY - nOverlapY);
			//	  double occNormF = time256/(256*ml);
			MakeVectorOcclusionMaskTime(mvClipF, nBlkX, nBlkY, ml, 1.0,
				nPel, MaskSmallF, nBlkXP, time256,
				nBlkSizeX - nOverlapX, nBlkSizeY - nOverlapY);
		}
		else
		{
			class OpFwd
			{
			public:
				static inline int compute (int t) { return (      t); }
			};

			class OpBkw
			{
			public:
				static inline int compute (int t) { return (256 - t); }
			};

			MakeVectorOcclusionMaskTimePlane <OpBkw> (
				mvClipB, nBlkX, nBlkY, ml,
				nPel, MaskSmallB, nBlkXP, pt256[0], nt256Pitches[0],
				nBlkSizeX - nOverlapX, nBlkSizeY - nOverlapY);
			MakeVectorOcclusionMaskTimePlane <OpFwd> (
				mvClipF, nBlkX, nBlkY, ml,
				nPel, MaskSmallF, nBlkXP, pt256[0], nt256Pitches[0],
				nBlkSizeX - nOverlapX, nBlkSizeY - nOverlapY);
		}
		if (nBlkXP > nBlkX) // fill right
		{
			for (int j=0; j<nBlkY; j++)
			{
				MaskSmallB[j*nBlkXP + nBlkX] = MaskSmallB[j*nBlkXP + nBlkX-1];
				MaskSmallF[j*nBlkXP + nBlkX] = MaskSmallF[j*nBlkXP + nBlkX-1];
			}
		}
		if (nBlkYP > nBlkY) // fill bottom
		{
			for (int i=0; i<nBlkXP; i++)
			{
				MaskSmallB[nBlkXP*nBlkY +i] = MaskSmallB[nBlkXP*(nBlkY-1) +i];
				MaskSmallF[nBlkXP*nBlkY +i] = MaskSmallF[nBlkXP*(nBlkY-1) +i];
			}
		}
		// upsize (bilinear interpolate) vector masks to fullframe size


		int dummyplane = PLANAR_Y; // use luma plane resizer code for all planes if we resize from luma small mask
		upsizer->SimpleResizeDo(VXFullYB, nWidthP, nHeightP, VPitchY, VXSmallYB, nBlkXP, nBlkXP, dummyplane);
		upsizer->SimpleResizeDo(VYFullYB, nWidthP, nHeightP, VPitchY, VYSmallYB, nBlkXP, nBlkXP, dummyplane);
		upsizerUV->SimpleResizeDo(VXFullUVB, nWidthPUV, nHeightPUV, VPitchUV, VXSmallUVB, nBlkXP, nBlkXP, dummyplane);
		upsizerUV->SimpleResizeDo(VYFullUVB, nWidthPUV, nHeightPUV, VPitchUV, VYSmallUVB, nBlkXP, nBlkXP, dummyplane);

		upsizer->SimpleResizeDo(VXFullYF, nWidthP, nHeightP, VPitchY, VXSmallYF, nBlkXP, nBlkXP, dummyplane);
		upsizer->SimpleResizeDo(VYFullYF, nWidthP, nHeightP, VPitchY, VYSmallYF, nBlkXP, nBlkXP, dummyplane);
		upsizerUV->SimpleResizeDo(VXFullUVF, nWidthPUV, nHeightPUV, VPitchUV, VXSmallUVF, nBlkXP, nBlkXP, dummyplane);
		upsizerUV->SimpleResizeDo(VYFullUVF, nWidthPUV, nHeightPUV, VPitchUV, VYSmallUVF, nBlkXP, nBlkXP, dummyplane);

		upsizer->SimpleResizeDo(MaskFullYB, nWidthP, nHeightP, VPitchY, MaskSmallB, nBlkXP, nBlkXP, dummyplane);
		upsizerUV->SimpleResizeDo(MaskFullUVB, nWidthPUV, nHeightPUV, VPitchUV, MaskSmallB, nBlkXP, nBlkXP, dummyplane);

		upsizer->SimpleResizeDo(MaskFullYF, nWidthP, nHeightP, VPitchY, MaskSmallF, nBlkXP, nBlkXP, dummyplane);
		upsizerUV->SimpleResizeDo(MaskFullUVF, nWidthPUV, nHeightPUV, VPitchUV, MaskSmallF, nBlkXP, nBlkXP, dummyplane);


		// Get motion info from more frames for occlusion areas
		PVideoFrame mvFF = mvClipF.GetFrame(n, env);
		mvClipF.Update(mvFF, env);// forward from prev to cur
		mvFF = 0;
		PVideoFrame mvBB = mvClipB.GetFrame(nref, env);
		mvClipB.Update(mvBB, env);// backward from next next to next
		mvBB = 0;

		if ( mvClipB.IsUsable()  && mvClipF.IsUsable() )
		{
			// get vector mask from extra frames
			MakeVectorSmallMasks(mvClipB, nBlkX, nBlkY, VXSmallYBB, nBlkXP, VYSmallYBB, nBlkXP);
			MakeVectorSmallMasks(mvClipF, nBlkX, nBlkY, VXSmallYFF, nBlkXP, VYSmallYFF, nBlkXP);
			if (nBlkXP > nBlkX) // fill right
			{
				for (int j=0; j<nBlkY; j++)
				{
					VXSmallYBB[j*nBlkXP + nBlkX] = std::min(VXSmallYBB[j*nBlkXP + nBlkX-1], uint8_t (128));
					VYSmallYBB[j*nBlkXP + nBlkX] = VYSmallYBB[j*nBlkXP + nBlkX-1];
					VXSmallYFF[j*nBlkXP + nBlkX] = std::min(VXSmallYFF[j*nBlkXP + nBlkX-1], uint8_t (128));
					VYSmallYFF[j*nBlkXP + nBlkX] = VYSmallYFF[j*nBlkXP + nBlkX-1];
				}
			}
			if (nBlkYP > nBlkY) // fill bottom
			{
				for (int i=0; i<nBlkXP; i++)
				{
					VXSmallYBB[nBlkXP*nBlkY +i] = VXSmallYBB[nBlkXP*(nBlkY-1) +i];
					VYSmallYBB[nBlkXP*nBlkY +i] = std::min(VYSmallYBB[nBlkXP*(nBlkY-1) +i], uint8_t (128));
					VXSmallYFF[nBlkXP*nBlkY +i] = VXSmallYFF[nBlkXP*(nBlkY-1) +i];
					VYSmallYFF[nBlkXP*nBlkY +i] = std::min(VYSmallYFF[nBlkXP*(nBlkY-1) +i], uint8_t (128));
				}
			}
			VectorSmallMaskYToHalfUV(VXSmallYBB, nBlkXP, nBlkYP, VXSmallUVBB, 2);
			VectorSmallMaskYToHalfUV(VYSmallYBB, nBlkXP, nBlkYP, VYSmallUVBB, yRatioUV);
			VectorSmallMaskYToHalfUV(VXSmallYFF, nBlkXP, nBlkYP, VXSmallUVFF, 2);
			VectorSmallMaskYToHalfUV(VYSmallYFF, nBlkXP, nBlkYP, VYSmallUVFF, yRatioUV);

			// upsize vectors to full frame
			upsizer->SimpleResizeDo(VXFullYBB, nWidthP, nHeightP, VPitchY, VXSmallYBB, nBlkXP, nBlkXP, dummyplane);
			upsizer->SimpleResizeDo(VYFullYBB, nWidthP, nHeightP, VPitchY, VYSmallYBB, nBlkXP, nBlkXP, dummyplane);
			upsizerUV->SimpleResizeDo(VXFullUVBB, nWidthPUV, nHeightPUV, VPitchUV, VXSmallUVBB, nBlkXP, nBlkXP, dummyplane);
			upsizerUV->SimpleResizeDo(VYFullUVBB, nWidthPUV, nHeightPUV, VPitchUV, VYSmallUVBB, nBlkXP, nBlkXP, dummyplane);

			upsizer->SimpleResizeDo(VXFullYFF, nWidthP, nHeightP, VPitchY, VXSmallYFF, nBlkXP, nBlkXP, dummyplane);
			upsizer->SimpleResizeDo(VYFullYFF, nWidthP, nHeightP, VPitchY, VYSmallYFF, nBlkXP, nBlkXP, dummyplane);
			upsizerUV->SimpleResizeDo(VXFullUVFF, nWidthPUV, nHeightPUV, VPitchUV, VXSmallUVFF, nBlkXP, nBlkXP, dummyplane);
			upsizerUV->SimpleResizeDo(VYFullUVFF, nWidthPUV, nHeightPUV, VPitchUV, VYSmallUVFF, nBlkXP, nBlkXP, dummyplane);

			if (timeclip == 0)
			{
				FlowInterExtra(pDst[0], nDstPitches[0], pRef[0] + nOffsetY, pSrc[0] + nOffsetY, nRefPitches[0],
					VXFullYB, VXFullYF, VYFullYB, VYFullYF, MaskFullYB, MaskFullYF, VPitchY,
					nWidth, nHeight, nPel, t256_prov_cst,
					VXFullYBB, VXFullYFF, VYFullYBB, VYFullYFF);
				FlowInterExtra(pDst[1], nDstPitches[1], pRef[1] + nOffsetUV, pSrc[1] + nOffsetUV, nRefPitches[1],
					VXFullUVB, VXFullUVF, VYFullUVB, VYFullUVF, MaskFullUVB, MaskFullUVF, VPitchUV,
					nWidthUV, nHeightUV, nPel, t256_prov_cst,
					VXFullUVBB, VXFullUVFF, VYFullUVBB, VYFullUVFF);
				FlowInterExtra(pDst[2], nDstPitches[2], pRef[2] + nOffsetUV, pSrc[2] + nOffsetUV, nRefPitches[2],
					VXFullUVB, VXFullUVF, VYFullUVB, VYFullUVF, MaskFullUVB, MaskFullUVF, VPitchUV,
					nWidthUV, nHeightUV, nPel, t256_prov_cst,
					VXFullUVBB, VXFullUVFF, VYFullUVBB, VYFullUVFF);
			}
			else
			{
				FlowInterExtra(pDst[0], nDstPitches[0], pRef[0] + nOffsetY, pSrc[0] + nOffsetY, nRefPitches[0],
					VXFullYB, VXFullYF, VYFullYB, VYFullYF, MaskFullYB, MaskFullYF, VPitchY,
					nWidth, nHeight, nPel,
					Time256ProviderPlane (pt256 [0], nt256Pitches [0], LUTVB, LUTVF),
					VXFullYBB, VXFullYFF, VYFullYBB, VYFullYFF);
				FlowInterExtra(pDst[1], nDstPitches[1], pRef[1] + nOffsetUV, pSrc[1] + nOffsetUV, nRefPitches[1],
					VXFullUVB, VXFullUVF, VYFullUVB, VYFullUVF, MaskFullUVB, MaskFullUVF, VPitchUV,
					nWidthUV, nHeightUV, nPel,
					Time256ProviderPlane (pt256 [1], nt256Pitches [1], LUTVB, LUTVF),
					VXFullUVBB, VXFullUVFF, VYFullUVBB, VYFullUVFF);
				FlowInterExtra(pDst[2], nDstPitches[2], pRef[2] + nOffsetUV, pSrc[2] + nOffsetUV, nRefPitches[2],
					VXFullUVB, VXFullUVF, VYFullUVB, VYFullUVF, MaskFullUVB, MaskFullUVF, VPitchUV,
					nWidthUV, nHeightUV, nPel,
					Time256ProviderPlane (pt256 [2], nt256Pitches [2], LUTVB, LUTVF),
					VXFullUVBB, VXFullUVFF, VYFullUVBB, VYFullUVFF);
			}
		}
		else // bad extra frames, use old method without extra frames
		{
			if (timeclip == 0)
			{
				FlowInter(pDst[0], nDstPitches[0], pRef[0] + nOffsetY, pSrc[0] + nOffsetY, nRefPitches[0],
					VXFullYB, VXFullYF, VYFullYB, VYFullYF, MaskFullYB, MaskFullYF, VPitchY,
					nWidth, nHeight, nPel, t256_prov_cst);
				FlowInter(pDst[1], nDstPitches[1], pRef[1] + nOffsetUV, pSrc[1] + nOffsetUV, nRefPitches[1],
					VXFullUVB, VXFullUVF, VYFullUVB, VYFullUVF, MaskFullUVB, MaskFullUVF, VPitchUV,
					nWidthUV, nHeightUV, nPel, t256_prov_cst);
				FlowInter(pDst[2], nDstPitches[2], pRef[2] + nOffsetUV, pSrc[2] + nOffsetUV, nRefPitches[2],
					VXFullUVB, VXFullUVF, VYFullUVB, VYFullUVF, MaskFullUVB, MaskFullUVF, VPitchUV,
					nWidthUV, nHeightUV, nPel, t256_prov_cst);
			}
			else
			{
				FlowInter(pDst[0], nDstPitches[0], pRef[0] + nOffsetY, pSrc[0] + nOffsetY, nRefPitches[0],
					VXFullYB, VXFullYF, VYFullYB, VYFullYF, MaskFullYB, MaskFullYF, VPitchY,
					nWidth, nHeight, nPel,
					Time256ProviderPlane (pt256 [0], nt256Pitches [0], LUTVB, LUTVF));
				FlowInter(pDst[1], nDstPitches[1], pRef[1] + nOffsetUV, pSrc[1] + nOffsetUV, nRefPitches[1],
					VXFullUVB, VXFullUVF, VYFullUVB, VYFullUVF, MaskFullUVB, MaskFullUVF, VPitchUV,
					nWidthUV, nHeightUV, nPel,
					Time256ProviderPlane (pt256 [1], nt256Pitches [1], LUTVB, LUTVF));
				FlowInter(pDst[2], nDstPitches[2], pRef[2] + nOffsetUV, pSrc[2] + nOffsetUV, nRefPitches[2],
					VXFullUVB, VXFullUVF, VYFullUVB, VYFullUVF, MaskFullUVB, MaskFullUVF, VPitchUV,
					nWidthUV, nHeightUV, nPel,
					Time256ProviderPlane (pt256 [2], nt256Pitches [2], LUTVB, LUTVF));
			}
		}

		if ( (pixelType & VideoInfo::CS_YUY2) == VideoInfo::CS_YUY2 && !planar)
		{
			YUY2FromPlanes(pDstYUY2, nDstPitchYUY2, nWidth, nHeight,
				pDst[0], nDstPitches[0], pDst[1], pDst[2], nDstPitches[1], isse);
		}
		return dst;
	}

	else
	{
		// poor estimation

		// prepare pointers
		PVideoFrame src = child->GetFrame(n,env); // it is easy to use child here - v2.0

		if (blend) //let's blend src with ref frames like ConvertFPS
		{
			PVideoFrame ref = child->GetFrame(nref,env);
			if (timeclip != 0)
			{
				t256 = timeclip->GetFrame (n, env);
			}

			if ( (pixelType & VideoInfo::CS_YUY2) == VideoInfo::CS_YUY2 )
			{
				pSrc[0] = src->GetReadPtr(); // we can blend YUY2
				nSrcPitches[0] = src->GetPitch();
				pRef[0] = ref->GetReadPtr();
				nRefPitches[0]  = ref->GetPitch();
				pDstYUY2 = dst->GetWritePtr();
				nDstPitchYUY2 = dst->GetPitch();

				if (timeclip == 0)
				{
					const Time256ProviderCst	t256_provider (time256, 0, 0);
					Blend(pDstYUY2, pSrc[0], pRef[0], nHeight, nWidth*2,
						nDstPitchYUY2, nSrcPitches[0], nRefPitches[0],
						t256_provider, isse);
				}
				else
				{
					pt256[0] = t256->GetReadPtr();
					nt256Pitches[0] = t256->GetPitch();

					Blend(pDstYUY2, pSrc[0], pRef[0], nHeight, nWidth*2,
						nDstPitchYUY2, nSrcPitches[0], nRefPitches[0],
						Time256ProviderPlane (pt256[0], nt256Pitches[0], 0, 0), isse);
				}
			}

			else
			{
				pDst[0] = YWPLAN(dst);
				pDst[1] = UWPLAN(dst);
				pDst[2] = VWPLAN(dst);
				nDstPitches[0] = YPITCH(dst);
				nDstPitches[1] = UPITCH(dst);
				nDstPitches[2] = VPITCH(dst);

				pRef[0] = YRPLAN(ref);
				pRef[1] = URPLAN(ref);
				pRef[2] = VRPLAN(ref);
				nRefPitches[0] = YPITCH(ref);
				nRefPitches[1] = UPITCH(ref);
				nRefPitches[2] = VPITCH(ref);

				pSrc[0] = YRPLAN(src);
				pSrc[1] = URPLAN(src);
				pSrc[2] = VRPLAN(src);
				nSrcPitches[0] = YPITCH(src);
				nSrcPitches[1] = UPITCH(src);
				nSrcPitches[2] = VPITCH(src);

				// blend with time weight
				if (timeclip == 0)
				{
					Blend(pDst[0], pSrc[0], pRef[0], nHeight, nWidth,
						nDstPitches[0], nSrcPitches[0], nRefPitches[0],
						t256_prov_cst, isse);
					Blend(pDst[1], pSrc[1], pRef[1], nHeightUV, nWidthUV,
						nDstPitches[1], nSrcPitches[1], nRefPitches[1],
						t256_prov_cst, isse);
					Blend(pDst[2], pSrc[2], pRef[2], nHeightUV, nWidthUV,
						nDstPitches[2], nSrcPitches[2], nRefPitches[2],
						t256_prov_cst, isse);
				}
				else
				{
					pt256[0] = YRPLAN(t256);
					pt256[1] = URPLAN(t256);
					pt256[2] = VRPLAN(t256);
					nt256Pitches[0] = YPITCH(t256);
					nt256Pitches[1] = UPITCH(t256);
					nt256Pitches[2] = VPITCH(t256);

					Blend(pDst[0], pSrc[0], pRef[0], nHeight, nWidth,
						nDstPitches[0], nSrcPitches[0], nRefPitches[0],
						Time256ProviderPlane (pt256[0], nt256Pitches[0], 0, 0), isse);
					Blend(pDst[1], pSrc[1], pRef[1], nHeightUV, nWidthUV,
						nDstPitches[1], nSrcPitches[1], nRefPitches[1],
						Time256ProviderPlane (pt256[1], nt256Pitches[1], 0, 0), isse);
					Blend(pDst[2], pSrc[2], pRef[2], nHeightUV, nWidthUV,
						nDstPitches[2], nSrcPitches[2], nRefPitches[2],
						Time256ProviderPlane (pt256[2], nt256Pitches[2], 0, 0), isse);
				}
			}
			return dst;
		}

		else
		{
			return src; // like ChangeFPS
		}
	}
}
