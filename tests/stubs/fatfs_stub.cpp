//
// fatfs_stub.cpp
//
// Controllable stub implementations for FatFS functions.
//

#include <cstring>
#include <algorithm>
#include "fatfs/ff.h"

// ---------------------------------------------------------------------------
// Test control globals
// ---------------------------------------------------------------------------
bool                 g_fatfs_open_fail  = false;
bool                 g_fatfs_read_fail  = false;
bool                 g_fatfs_stat_fail  = false;
bool                 g_fatfs_write_fail = false;
const unsigned char* g_fatfs_data       = nullptr;
size_t               g_fatfs_data_size  = 0;

// Write capture: tests read these to verify file contents
unsigned char        g_fatfs_written_buf[kFatfsWriteBufSize] = {};
size_t               g_fatfs_written_size = 0;
FSIZE_t              g_fatfs_seek_pos     = 0;

// ---------------------------------------------------------------------------
// FatFS stub implementations
// ---------------------------------------------------------------------------
FRESULT f_open(FIL* /*fp*/, const char* /*path*/, int /*mode*/)
{
	return g_fatfs_open_fail ? 1 : FR_OK;
}

FRESULT f_close(FIL* /*fp*/)
{
	return FR_OK;
}

FSIZE_t f_size(FIL* /*fp*/)
{
	return static_cast<FSIZE_t>(g_fatfs_data_size);
}

FRESULT f_read(FIL* /*fp*/, void* buf, UINT btr, UINT* br)
{
	if (g_fatfs_read_fail) { *br = 0; return 1; }
	UINT n = static_cast<UINT>(std::min(static_cast<size_t>(btr), g_fatfs_data_size));
	if (g_fatfs_data && n > 0)
		std::memcpy(buf, g_fatfs_data, n);
	*br = n;
	return FR_OK;
}

FRESULT f_stat(const char* /*path*/, FILINFO* fno)
{
	if (g_fatfs_stat_fail)
		return FR_NO_FILE;
	if (fno)
		fno->fsize = static_cast<FSIZE_t>(g_fatfs_data_size);
	return FR_OK;
}

FRESULT f_write(FIL* /*fp*/, const void* buf, UINT btw, UINT* bw)
{
	if (g_fatfs_write_fail) { if (bw) *bw = 0; return 1; }
	UINT n = static_cast<UINT>(
		std::min(static_cast<size_t>(btw),
			kFatfsWriteBufSize - static_cast<size_t>(g_fatfs_seek_pos)));
	if (n > 0)
		std::memcpy(g_fatfs_written_buf + g_fatfs_seek_pos, buf, n);
	g_fatfs_seek_pos += n;
	if (g_fatfs_seek_pos > static_cast<FSIZE_t>(g_fatfs_written_size))
		g_fatfs_written_size = static_cast<size_t>(g_fatfs_seek_pos);
	if (bw)
		*bw = n;
	return FR_OK;
}

FRESULT f_lseek(FIL* /*fp*/, FSIZE_t ofs)
{
	if (ofs <= kFatfsWriteBufSize)
		g_fatfs_seek_pos = ofs;
	return FR_OK;
}

FSIZE_t f_tell(FIL* /*fp*/)
{
	return g_fatfs_seek_pos;
}
