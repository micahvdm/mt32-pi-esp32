#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <cstring>

#include "lcd/drivers/ssd1327.h"
#include "lcd/font6x8.h"

LOGMODULE("ssd1327");

CSSD1327::CSSD1327(CI2CMaster* pI2CMaster, u8 nAddress, u8 nWidth, u8 nHeight, TLCDRotation Rotation, TLCDMirror Mirror)
	: CLCD(nWidth, nHeight),
	  m_pI2CMaster(pI2CMaster),
	  m_nAddress(nAddress),
	  m_Rotation(Rotation),
	  m_Mirror(Mirror)
{
	// 128 * 128 / 8 = 2048 bytes for 1-bit buffer
	m_nBufferSize = (static_cast<size_t>(nWidth) * nHeight) / 8;
	m_pBuffer = new u8[m_nBufferSize];
	memset(m_pBuffer, 0, m_nBufferSize);
}

bool CSSD1327::GetBufferPixel(u8 x, u8 y) const
{
	u16 nIndex = (y * (m_nWidth / 8)) + (x / 8);
	u8  nBit   = 7 - (x % 8);
	return (m_pBuffer[nIndex] & (1 << nBit)) != 0;
}

CSSD1327::~CSSD1327()
{
	delete[] m_pBuffer;
}

void CSSD1327::WriteCommand(u8 nCommand) const
{
	u8 buffer[2] = { 0x00, nCommand }; // Co=0, D/C#=0
	m_pI2CMaster->Write(m_nAddress, buffer, sizeof(buffer));
}

void CSSD1327::WriteCommand(const u8* pData, size_t nSize) const
{
	u8 buffer[16];
	buffer[0] = 0x00; // Co=0, D/C#=0
	memcpy(buffer + 1, pData, nSize > 15 ? 15 : nSize);
	m_pI2CMaster->Write(m_nAddress, buffer, nSize + 1);
}

void CSSD1327::SendData(const u8* pData, size_t nSize)
{
	// SSD1327 I2C data prefix: Co=0, D/C#=1 -> 0x40
	// Using a stack buffer instead of the heap to avoid fragmentation in hot loops
	u8 buffer[129]; // Max row size + 1
	buffer[0] = 0x40;
	memcpy(buffer + 1, pData, nSize > 128 ? 128 : nSize);
	m_pI2CMaster->Write(m_nAddress, buffer, nSize + 1);
}

bool CSSD1327::Initialize()
{
	LOGNOTE("Initializing SSD1327 at I2C address 0x%02x", m_nAddress);

	WriteCommand(0xAE); // Display OFF

	// Set column address: 0 to 63 (covers 128 pixels at 2 pixels/byte)
	u8 colAddr[] = { 0x15, 0x00, 0x3F };
	WriteCommand(colAddr, 3);

	// Set row address: 0 to 127
	u8 rowAddr[] = { 0x75, 0x00, 0x7F };
	WriteCommand(rowAddr, 3);

	WriteCommand(0x81); // Set Contrast
	WriteCommand(0x80);

	// Re-map setting (Command 0xA0)
	// 0x42: Horizontal increment, Column Remap enabled, COM Split enabled.
	// This is the "baseline" orientation. We toggle bits relative to this.
	u8 nRemap = 0x42; 

	if (m_Rotation == TLCDRotation::Inverted) nRemap ^= 0x12; // Flip COM and Column
	if (m_Mirror == TLCDMirror::Mirrored)     nRemap ^= 0x02; // Flip Column remap

	u8 remapSeq[] = { 0xA0, nRemap };
	WriteCommand(remapSeq, 2);

	WriteCommand(0xA1); WriteCommand(0x00); // Start line
	WriteCommand(0xA2); WriteCommand(0x00); // Display offset
	WriteCommand(0xA4);                     // Normal display
	WriteCommand(0xA8); WriteCommand(0x7F); // 128 MUX
	WriteCommand(0xAB); WriteCommand(0x01); // Enable internal VDD
	WriteCommand(0xB1); WriteCommand(0xF1); // Phase length
	WriteCommand(0xB3); WriteCommand(0x00); // Oscillator freq
	WriteCommand(0xB6); WriteCommand(0x0F); // Pre-charge
	WriteCommand(0xBC); WriteCommand(0x08); // Pre-charge voltage
	WriteCommand(0xBE); WriteCommand(0x07); // VCOMH
	WriteCommand(0xBF); WriteCommand(0x02); // Enable internal VSL

	WriteCommand(0xAF); // Display ON

	Clear(true);
	return true;
}

void CSSD1327::Clear(bool bUpdate)
{
	memset(m_pBuffer, 0, m_nBufferSize);
	if (bUpdate)
		Flip();
}

void CSSD1327::SetPixel(u8 x, u8 y, bool bOn)
{
	// Direct 1:1 mapping to the internal buffer. 
	// Hardware Re-map (0x51) handles the 90-degree physical rotation.
	if (x >= m_nWidth || y >= m_nHeight)
		return;

	// Software rotation: Rotate 90 degrees clockwise to fix "on its side" font
	// UI X advance becomes Hardware Y advance
	// UI Y advance becomes Hardware 127 - X advance
	u8 hwX = 127 - y;
	u8 hwY = x;

	u16 nIndex = (hwY * (m_nWidth / 8)) + (hwX / 8);
	u8  nBit   = 7 - (hwX % 8);

	if (bOn)
		m_pBuffer[nIndex] |= (1 << nBit);
	else
		m_pBuffer[nIndex] &= ~(1 << nBit);
}

bool CSSD1327::GetPixel(u8 x, u8 y) const
{
	if (x >= m_nWidth || y >= m_nHeight)
		return false;

	// Apply the same rotation as SetPixel
	u8 hwX = 127 - y;
	u8 hwY = x;

	u16 nIndex = (hwY * (m_nWidth / 8)) + (hwX / 8);
	u8  nBit   = 7 - (hwX % 8);
	return (m_pBuffer[nIndex] & (1 << nBit)) != 0;
}

void CSSD1327::Flip()
{
	// Reset pointers to start of display
	u8 col[] = { 0x15, 0x00, 0x3F }; WriteCommand(col, 3);
	u8 row[] = { 0x75, 0x00, 0x7F }; WriteCommand(row, 3);

	// Convert 1-bit buffer to 4-bit nibbles and send row by row
	for (u8 y = 0; y < m_nHeight; y++)
	{
		u8 row[64]; // 128 pixels / 2 pixels per byte
		for (u8 x = 0; x < 64; x++)
		{
			// Map monochrome to 4-bit grayscale (0x0 or 0xF)
			u8 p1 = GetBufferPixel(x * 2,     y) ? 0xF0 : 0x00;
			u8 p2 = GetBufferPixel(x * 2 + 1, y) ? 0x0F : 0x00;
			row[x] = p1 | p2;
		}
		SendData(row, sizeof(row));
	}
}

void CSSD1327::Print(const char* pText, u8 x, u8 y, bool bInverted, bool bUpdate)
{
	// This uses the project's standard font handling
	while (*pText)
	{
		u8 nCharacter = static_cast<u8>(*pText++);
		if (nCharacter < 0x20 || nCharacter > 0x7F) nCharacter = ' ';

		for (u8 i = 0; i < 6; i++) // Standard Font6x8 width
		{
			u8 nLine = Font6x8[nCharacter - 0x20][i];
			for (u8 j = 0; j < 8; j++) // Standard Font6x8 height
			{
				bool bOn = (nLine >> j) & 0x01;
				SetPixel(x + i, (y * 8) + j, bInverted ? !bOn : bOn);
			}
		}
		x += 6;
	}

	if (bUpdate)
		Flip();
}

void CSSD1327::DrawImage(TImage Image, bool bUpdate)
{
	// Logic for drawing the mt32-pi logo or other assets
	// (Simplified for this example; usually involves copying raw bits)
	if (bUpdate)
		Flip();
}

void CSSD1327::DrawFilledRect(u8 x1, u8 y1, u8 x2, u8 y2, bool bUpdate)
{
	u8 nLeft   = Utility::Min(x1, x2);
	u8 nRight  = Utility::Max(x1, x2);
	u8 nTop    = Utility::Min(y1, y2);
	u8 nBottom = Utility::Max(y1, y2);

	for (u8 y = nTop; y <= nBottom; y++)
	{
		for (u8 x = nLeft; x <= nRight; x++)
		{
			SetPixel(x, y, true);
		}
	}

	if (bUpdate)
		Flip();
}