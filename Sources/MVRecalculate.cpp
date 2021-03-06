// MVTools
// Recalculate motion data (based on MVAnalyse)
// Copyright(c)2008 A.G.Balakhnin aka Fizick

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

#include "AnaFlags.h"
#include "ClipFnc.h"
#include "cpu.h"
#include "dctfftw.h"
#include "dctint.h"
#include "MVClip.h"
#include "MVGroupOfFrames.h"
#include "MVRecalculate.h"
#include "profile.h"
#include "SuperParams64Bits.h"

#include	<algorithm>

#include <cmath>
#include <cstdio>


//extern MVCore mvCore;

MVRecalculate::MVRecalculate (
	PClip _super, PClip _vectors, int _thSAD, int _smooth,
	int _blksizex, int _blksizey, int st, int stp, int lambda, bool chroma,
   int _pnew, int _overlapx, int _overlapy, const char* _outfilename,
	int _dctmode, int _divide, int _sadx264, bool _isse, bool _meander,
	int trad, bool mt_flag, IScriptEnvironment* env
)
:	GenericVideoFilter (_super)
,	_srd_arr ()
,	_vectorfields_aptr ()
,	_dct_factory_ptr ()
,	_dct_pool ()
,	_nbr_srd ((trad > 0) ? trad * 2 : 1)
,	_mt_flag (mt_flag)
{
	_srd_arr.resize (_nbr_srd);
	for (int srd_index = 0; srd_index < _nbr_srd; ++srd_index)
	{
		SrcRefData &	srd = _srd_arr [srd_index];
		srd._clip_sptr = SharedPtr <MVClip> (
			new MVClip (_vectors, 999999, 255, env, _nbr_srd, srd_index)
		);
	}

	MVAnalysisData &	analysisData        = _srd_arr [0]._analysis_data;
   MVAnalysisData &	analysisDataDivided = _srd_arr [0]._analysis_data_divided;

	vi.num_frames *= _nbr_srd;
	vi.MulDivFPS (_nbr_srd, 1);

	smooth = _smooth;

	SuperParams64Bits	params;
	memcpy (&params, &child->GetVideoInfo ().num_audio_samples, 8);
	const int		nHeight       = params.nHeight;
	const int		nSuperHPad    = params.nHPad;
	const int		nSuperVPad    = params.nVPad;
	const int		nSuperPel     = params.nPel;
	const int		nSuperModeYUV = params.nModeYUV;
	const int		nSuperLevels  = params.nLevels;


	nModeYUV = chroma ? YUVPLANES : YPLANE;
	if ((nModeYUV & nSuperModeYUV) != nModeYUV)
	{
		env->ThrowError (
			"MRecalculate: super clip does not contain needed color data"
		);
	}

#if !defined(_WIN64)
	MVAnalysisData *	pAnalyseFilter =
		reinterpret_cast <MVAnalysisData *> (_vectors->GetVideoInfo().nchannels);
#else
    uintptr_t p = (((uintptr_t)(unsigned int)_vectors->GetVideoInfo().nchannels ^ 0x80000000) << 32) | (uintptr_t)(unsigned int)_vectors->GetVideoInfo().sample_type;
    MVAnalysisData *pAnalyseFilter = reinterpret_cast<MVAnalysisData *>(p);
#endif

	analysisData.nWidth    = pAnalyseFilter->GetWidth();
	analysisData.nHeight   = pAnalyseFilter->GetHeight();
	analysisData.pixelType = pAnalyseFilter->GetPixelType();
	analysisData.yRatioUV  = (vi.IsYV12 ()) ? 2 : 1;
	analysisData.xRatioUV  = 2;	// for YV12 and YUY2, really do not used and assumed to 2

	pSrcGOF = new MVGroupOfFrames (
		nSuperLevels, analysisData.nWidth, analysisData.nHeight,
		nSuperPel, nSuperHPad, nSuperVPad, nSuperModeYUV,
		_isse, analysisData.yRatioUV, mt_flag
	);
	pRefGOF = new MVGroupOfFrames (
		nSuperLevels, analysisData.nWidth, analysisData.nHeight,
		nSuperPel, nSuperHPad, nSuperVPad, nSuperModeYUV,
		_isse, analysisData.yRatioUV, mt_flag
	);
	const int		nSuperWidth  = child->GetVideoInfo().width;
	const int		nSuperHeight = child->GetVideoInfo().height;

	if (   nHeight                      != analysisData.nHeight
	    || nSuperWidth - 2 * nSuperHPad != analysisData.nWidth)
	{
		env->ThrowError("MRecalculate : wrong frame size");
	}
	if (vi.pixel_type != analysisData.pixelType)
	{
		env->ThrowError("MRecalculate: wrong pixel type");
	}

	analysisData.nBlkSizeX = _blksizex;
	analysisData.nBlkSizeY = _blksizey;
	if (   (analysisData.nBlkSizeX !=  4 || analysisData.nBlkSizeY !=  4)
	    && (analysisData.nBlkSizeX !=  8 || analysisData.nBlkSizeY !=  4)
	    && (analysisData.nBlkSizeX !=  8 || analysisData.nBlkSizeY !=  8)
	    && (analysisData.nBlkSizeX != 16 || analysisData.nBlkSizeY !=  2)
	    && (analysisData.nBlkSizeX != 16 || analysisData.nBlkSizeY !=  8)
	    && (analysisData.nBlkSizeX != 16 || analysisData.nBlkSizeY != 16)
	    && (analysisData.nBlkSizeX != 32 || analysisData.nBlkSizeY != 32)
	    && (analysisData.nBlkSizeX != 32 || analysisData.nBlkSizeY != 16))
	{
		env->ThrowError (
			"MVRecalculate: Block's size must be "
			"4x4, 8x4, 8x8, 16x2, 16x8, 16x16, 32x16, 32x32"
		);
	}

	analysisData.nPel        = nSuperPel;	//pAnalyseFilter->GetPel();
   analysisData.nDeltaFrame = pAnalyseFilter->GetDeltaFrame ();
	analysisData.isBackward  = pAnalyseFilter->IsBackward ();

   if (   _overlapx < 0 || _overlapx >= _blksizex
	    || _overlapy < 0 || _overlapy >= _blksizey)
	{
		env->ThrowError ("MRecalculate: overlap must be less than block size");
	}

   if (_overlapx % 2 || (_overlapy % 2 > 0 && vi.IsYV12 ()))
	{
		env->ThrowError ("MRecalculate: overlap must be more even");
	}

	if (_divide != 0 && (_blksizex < 8 && _blksizey < 8))
	{
		env->ThrowError (
			"MRecalculate: Block sizes must be 8 or more for divide mode"
		);
	}
   if (   _divide != 0
	    && (   (_overlapx % 4                    )
	        || (_overlapy % 4 > 0 && vi.IsYV12 ())
	        || (_overlapy % 2 > 0 && vi.IsYUY2 ())))
	{
		env->ThrowError("MRecalculate: overlap must be more even for divide mode");
	}

	divideExtra = _divide;

	// include itself, but usually equal to 256 :-)
	headerSize = std::max (int (4 + sizeof (analysisData)), 256);

	analysisData.nOverlapX = _overlapx;
	analysisData.nOverlapY = _overlapy;

	const int		nBlkX =   (analysisData.nWidth    - analysisData.nOverlapX)
						        / (analysisData.nBlkSizeX - analysisData.nOverlapX);
	const int		nBlkY =   (analysisData.nHeight   - analysisData.nOverlapY)
						        / (analysisData.nBlkSizeY - analysisData.nOverlapY);

	analysisData.nBlkX    = nBlkX;
	analysisData.nBlkY    = nBlkY;
	analysisData.nLvCount = 1;


	nLambda = lambda;
	pnew    = _pnew;
	meander = _meander;

   if (_dctmode != 0)
   {
		_dct_factory_ptr = std::auto_ptr <DCTFactory> (
			new DCTFactory (_dctmode, _isse, _blksizex, _blksizey, *env)
		);
		_dct_pool.set_factory (*_dct_factory_ptr);
   }

	switch (st)
	{
	case 0 :
		searchType   = ONETIME;
		nSearchParam = (stp < 1) ? 1 : stp;
		break;
	case 1 :
		searchType   = NSTEP;
		nSearchParam = (stp < 0) ? 0 : stp;
		break;
	case 3 :
		searchType   = EXHAUSTIVE;
		nSearchParam = (stp < 1) ? 1 : stp;
		break;
	case 4 :
		searchType   = HEX2SEARCH;
		nSearchParam = (stp < 1) ? 1 : stp;
		break;
	case 5 :
		searchType   = UMHSEARCH;
		nSearchParam = (stp < 1) ? 1 : stp; // really min is 4
		break;
	case 6 :
		searchType   = HSEARCH;
		nSearchParam = (stp < 1) ? 1 : stp;
		break;
	case 7 :
		searchType   = VSEARCH;
		nSearchParam = (stp < 1) ? 1 : stp;
		break;
	case 2 :
	default :
		searchType   = LOGARITHMIC;
		nSearchParam = (stp < 1) ? 1 : stp;
	}

	analysisData.nFlags  = 0;
	analysisData.nFlags |= (_isse) ? MOTION_USE_ISSE : 0;
	analysisData.nFlags |= (analysisData.isBackward) ? MOTION_IS_BACKWARD : 0;
	analysisData.nFlags |= (chroma) ? MOTION_USE_CHROMA_MOTION : 0;
	if (_sadx264 == 0)
	{
		analysisData.nFlags |= cpu_detect ();
	}
	else
	{
		if (_sadx264 > 0 && _sadx264 <= 12)
		{
			//force specific function
			analysisData.nFlags |= CPU_MMXEXT;
			analysisData.nFlags |= (_sadx264 ==  2) ? CPU_CACHELINE_32 : 0;
			analysisData.nFlags |= (_sadx264 ==  3 || _sadx264 ==  5 || _sadx264 ==  7) ? CPU_CACHELINE_64 : 0;
			analysisData.nFlags |= (_sadx264 ==  4 || _sadx264 ==  5 || _sadx264 == 10) ? CPU_SSE2_IS_FAST : 0;
			analysisData.nFlags |= (_sadx264 ==  6) ? CPU_SSE3 : 0;
			analysisData.nFlags |= (_sadx264 ==  7 || _sadx264 >= 11) ? CPU_SSSE3 : 0;
			//beta (debug)
			analysisData.nFlags |= (_sadx264 ==  8) ? MOTION_USE_SSD : 0;
			analysisData.nFlags |= (_sadx264 >=  9 && _sadx264 <= 12) ? MOTION_USE_SATD : 0;
			analysisData.nFlags |= (_sadx264 == 12) ? CPU_PHADD_IS_FAST : 0;
		}
	}

	_vectorfields_aptr = std::auto_ptr <GroupOfPlanes> (new GroupOfPlanes (
		analysisData.nBlkSizeX,
		analysisData.nBlkSizeY,
		analysisData.nLvCount,
		analysisData.nPel,
		analysisData.nFlags,
		analysisData.nOverlapX,
		analysisData.nOverlapY,
		analysisData.nBlkX,
		analysisData.nBlkY,
		analysisData.yRatioUV,
		divideExtra,
		(_dct_factory_ptr.get () != 0) ? &_dct_pool : 0,
		_mt_flag
	));

	analysisData.nMagicKey = MVAnalysisData::MOTION_MAGIC_KEY;
	analysisData.nHPadding = nSuperHPad;
	analysisData.nVPadding = nSuperVPad;

	// MVAnalysisData and outfile format version: last update v1.8.1
	analysisData.nVersion = MVAnalysisData::VERSION;

	outfilename = _outfilename;
	if (lstrlen (outfilename) > 0)
	{
		outfile = fopen(outfilename,"wb");
		if (outfile == NULL)
		{
			env->ThrowError ("MRecalculate: out file can not be created!");
		}
		else
		{
			fwrite (&analysisData, sizeof (analysisData), 1, outfile);
			// short vx, short vy, int SAD = 4 words = 8 bytes per block
			outfilebuf = new short [nBlkX * nBlkY * 4];
		}
	}
	else
	{
		outfile    = NULL;
		outfilebuf = NULL;
	}

	// Defines the format of the output vector clip
	const int		width_bytes = headerSize + _vectorfields_aptr->GetArraySize () * 4;
	ClipFnc::format_vector_clip (
		vi, true, nBlkX, "rgb32", width_bytes, "MRecalculate", *env
	);

	if (divideExtra)	//v1.8.1
	{
		memcpy (&analysisDataDivided, &analysisData, sizeof (analysisData));
		analysisDataDivided.nBlkX     = analysisData.nBlkX     * 2;
		analysisDataDivided.nBlkY     = analysisData.nBlkY     * 2;
		analysisDataDivided.nBlkSizeX = analysisData.nBlkSizeX / 2;
		analysisDataDivided.nBlkSizeY = analysisData.nBlkSizeY / 2;
		analysisDataDivided.nOverlapX = analysisData.nOverlapX / 2;
		analysisDataDivided.nOverlapY = analysisData.nOverlapY / 2;
		analysisDataDivided.nLvCount  = analysisData.nLvCount  + 1;
#if !defined(_WIN64)
		vi.nchannels = reinterpret_cast <uintptr_t> (&analysisDataDivided);
#else
		uintptr_t p = reinterpret_cast <uintptr_t> (&analysisDataDivided);
		vi.nchannels = 0x80000000L | (int)(p >> 32);
		vi.sample_type = (int)(p & 0xffffffffUL);
#endif
	}
	else
	{
		// we'll transmit to the processing filters a handle
		// on the analyzing filter itself ( it's own pointer ), in order
		// to activate the right parameters.
#if !defined(_WIN64)
		vi.nchannels = reinterpret_cast <uintptr_t> (&analysisData);
#else
		uintptr_t p = reinterpret_cast <uintptr_t> (&analysisData);
		vi.nchannels = 0x80000000L | (int)(p >> 32);
		vi.sample_type = (int)(p & 0xffffffffUL);
#endif
	}

	for (int srd_index = 1; srd_index < _nbr_srd; ++srd_index)
	{
		SrcRefData &	srd = _srd_arr [srd_index];
		srd._analysis_data         = analysisData;
		srd._analysis_data_divided = analysisDataDivided;
	}

	// normalize threshold to block size
	if (chroma)
	{
		thSAD =
			  _thSAD
			* (analysisData.nBlkSizeX * analysisData.nBlkSizeY)
			/ (8 * 8)
			* (1 + analysisData.yRatioUV)
			/      analysisData.yRatioUV;
	}
	else
	{
		thSAD =
			  _thSAD
			* (analysisData.nBlkSizeX * analysisData.nBlkSizeY)
			/ (8 * 8);
	}
}



MVRecalculate::~MVRecalculate()
{
	if (outfile != NULL)
	{
		fclose (outfile);
		outfile = 0;
		delete [] outfilebuf;
		outfilebuf = 0;
	}

	delete pSrcGOF;
	pSrcGOF = 0;
	delete pRefGOF;
	pRefGOF = 0;
}



PVideoFrame __stdcall MVRecalculate::GetFrame(int n, IScriptEnvironment* env)
{
	const int		nsrc      = n / _nbr_srd;
	const int		srd_index = n % _nbr_srd;

	SrcRefData &	srd = _srd_arr [srd_index];

	// get pointer to vectors
	::PVideoFrame	mvn = srd._clip_sptr->GetFrame (nsrc, env);
	srd._clip_sptr->Update (mvn, env);	// force calulation of vectors

	srd._analysis_data.nDeltaFrame = srd._clip_sptr->GetDeltaFrame ();
	srd._analysis_data.isBackward  = srd._clip_sptr->IsBackward ();

	srd._analysis_data_divided.nDeltaFrame = srd._analysis_data.nDeltaFrame;
	srd._analysis_data_divided.isBackward  = srd._analysis_data.isBackward;

	const int		nbr_src_frames = child->GetVideoInfo ().num_frames;
	int				minframe;
	int				maxframe;
	int				nref;
	if (srd._analysis_data.nDeltaFrame > 0)
	{
		const int		offset =
			  (srd._analysis_data.isBackward)
			?  srd._analysis_data.nDeltaFrame
			: -srd._analysis_data.nDeltaFrame;
		minframe =                  std::max (-offset, 0);
		maxframe = nbr_src_frames + std::min (-offset, 0);
		nref     = nsrc + offset;
	}
	else // special static mode
	{
		nref     = -srd._analysis_data.nDeltaFrame;	// positive fixed frame number
		minframe = 0;
		maxframe = nbr_src_frames;
	}

	::PVideoFrame		dst  = env->NewVideoFrame (vi);
	unsigned char *	pDst = dst->GetWritePtr ();

	// write analysis parameters as a header to frame
	memcpy (pDst, &headerSize, sizeof (int));
	if (divideExtra)
	{
		memcpy (
			pDst + sizeof (int),
			&srd._analysis_data_divided,
			sizeof (srd._analysis_data_divided)
		);
	}
	else
	{
		memcpy (
			pDst + sizeof(int),
			&srd._analysis_data,
			sizeof (srd._analysis_data)
		);
	}
	pDst += headerSize;

	if (! srd._clip_sptr->IsUsable () || nsrc < minframe || nsrc >= maxframe)
	{
		_vectorfields_aptr->WriteDefaultToArray (reinterpret_cast <int *> (pDst));
	}

	else
	{
//		DebugPrintf ("MVRecalculate: Get src frame %d",nsrc);
		::PVideoFrame	src = child->GetFrame (nsrc, env); // v2.0
		load_src_frame (*pSrcGOF, src, srd._analysis_data);

//		DebugPrintf("MVRecalculate: Get ref frame %d", nref);
		::PVideoFrame	ref = child->GetFrame (nref, env); // v2.0
		load_src_frame (*pRefGOF, ref, srd._analysis_data);

		const int		fieldShift = ClipFnc::compute_fieldshift (
			child,
			vi.IsFieldBased (),
			srd._analysis_data.nPel,
			nsrc,
			nref
		);

		if (outfile != NULL)
		{
			fwrite (&n, sizeof (int), 1, outfile);	// write frame number
		}

		_vectorfields_aptr->RecalculateMVs (
			*(srd._clip_sptr), pSrcGOF, pRefGOF,
			searchType, nSearchParam, nLambda, lsad, pnew,
			srd._analysis_data.nFlags, reinterpret_cast <int *> (pDst),
			outfilebuf, fieldShift, thSAD, smooth, meander
		);

		if (divideExtra)
		{
			// make extra level with divided sublocks with median (not estimated)
			// motion
			_vectorfields_aptr->ExtraDivide (
				reinterpret_cast <int *> (pDst),
				srd._analysis_data.nFlags
			);
		}

//		PROFILE_CUMULATE ();
		if (outfile != NULL)
		{
			fwrite (
				outfilebuf,
				sizeof (short) * 4 * srd._analysis_data.nBlkX
				                   * srd._analysis_data.nBlkY,
				1,
				outfile
			);
		}
	}

	return dst;
}



void	MVRecalculate::load_src_frame (MVGroupOfFrames &gof, ::PVideoFrame &src, const MVAnalysisData &ana_data)
{
	PROFILE_START (MOTION_PROFILE_YUY2CONVERT);
	const unsigned char *	pSrcY;
	const unsigned char *	pSrcU;
	const unsigned char *	pSrcV;
	int				nSrcPitchY;
	int				nSrcPitchUV;
	if ((ana_data.pixelType & VideoInfo::CS_YUY2) == VideoInfo::CS_YUY2)
	{
		// planar data packed to interleaved format (same as interleved2planar
		// by kassandro) - v2.0.0.5
		pSrcY       =         src->GetReadPtr ();
		pSrcU       = pSrcY + src->GetRowSize () / 2;
		pSrcV       = pSrcU + src->GetRowSize () / 4;
		nSrcPitchY  = src->GetPitch ();
		nSrcPitchUV = nSrcPitchY;
	}
	else
	{
		pSrcY       = src->GetReadPtr (PLANAR_Y);
		pSrcU       = src->GetReadPtr (PLANAR_U);
		pSrcV       = src->GetReadPtr (PLANAR_V);
		nSrcPitchY  = src->GetPitch (PLANAR_Y);
		nSrcPitchUV = src->GetPitch (PLANAR_U);
	}
	PROFILE_STOP (MOTION_PROFILE_YUY2CONVERT);

	gof.Update (
		nModeYUV,
		(BYTE*) pSrcY, nSrcPitchY,
		(BYTE*) pSrcU, nSrcPitchUV,
		(BYTE*) pSrcV, nSrcPitchUV
	); // v2.0
}

