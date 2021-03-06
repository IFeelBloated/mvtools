/*****************************************************************************

        types.h
        Author: Laurent de Soras, 2010

--- Legal stuff ---

This program is free software. It comes without any warranty, to
the extent permitted by applicable law. You can redistribute it
and/or modify it under the terms of the Do What The Fuck You Want
To Public License, Version 2, as published by Sam Hocevar. See
http://sam.zoy.org/wtfpl/COPYING for more details.

*Tab=3***********************************************************************/



#if ! defined (types_HEADER_INCLUDED)
#define	types_HEADER_INCLUDED
#pragma once

#if defined (_MSC_VER)
	#pragma warning (4 : 4250) // "Inherits via dominance."
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



typedef signed char			int8_t;
typedef unsigned char		uint8_t;
typedef signed short			int16_t;
typedef unsigned short		uint16_t;
typedef signed int			int32_t;
typedef unsigned int			uint32_t;
typedef unsigned __int64	uint64_t;
typedef __int64				int64_t;



#endif	// types_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
