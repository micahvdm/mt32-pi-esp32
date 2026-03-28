#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <cstring>

#include "lcd/drivers/ssd1327.h"
#include "lcd/font.h"

LOGMODULE("ssd1327");

CSSD1327::CSSD1327(CI2CMaster* pI2CMaster, u8 nAddress, u8 nWidth, u8 nHeight, TLCDRotation Rotation)
	: CLCD(nWidth, nHeight),
	  m_pI2CMaster(pI2CMaster),
	  m_nAddress(nAddress),
	  m_Rotation(Rotation)
{
	// 128 * 128 / 8 = 2048 bytes for 1-bit buffer
	m_nBufferSize = (static_cast<size_t>(nWidth) * nHeight) / 8;
	m_pBuffer = new u8[m_nBufferSize];
	memset(m_pBuffer, 0, m_nBufferSize);
}

CSSD1327::~CSSD1327()
{
	delete[] m_pBuffer;
}

void CSSD1327::SendCommand(u8 nCommand)
{
	u8 buffer[2] = { 0x00, nCommand }; // Co=0, D/C#=0
	m_pI2CMaster->Write(m_nAddress, buffer, sizeof(buffer));
}

void CSSD1327::SendData(const u8* pData, size_t nSize)
{
	// SSD1327 I2C data prefix: Co=0, D/C#=1 -> 0x40
	// We allocate a temp buffer to prepend the control byte for the I2C transaction
	u8* pBuffer = new u8[nSize + 1];
	pBuffer[0] = 0x40;
	memcpy(pBuffer + 1, pData, nSize);
	m_pI2CMaster->Write(m_nAddress, pBuffer, nSize + 1);
	delete[] pBuffer;
}

bool CSSD1327::Initialize()
{
	LOGNOTE("Initializing SSD1327 at I2C address 0x%02x", m_nAddress);

	SendCommand(0xAE); // Display OFF

	SendCommand(0x15); // Set Column Address
	SendCommand(0x00); // Start
	SendCommand(0x7F); // End

	SendCommand(0x75); // Set Row Address
	SendCommand(0x00); // Start
	SendCommand(0x7F); // End

	SendCommand(0x81); // Set Contrast
	SendCommand(0x80);

	SendCommand(0xA0); // Set Re-map
	// 0x51: Horizontal increment, Column address 0 mapped to SEG0,
	// Nibble remap (Pixel 0 = bits 4-7), Scan COM0 to COM[N-1]
	SendCommand(m_Rotation == TLCDRotation::Inverted ? 0x42 : 0x51); 

	SendCommand(0xA1); // Set Display Start Line
	SendCommand(0x00);

	SendCommand(0xA2); // Set Display Offset
	SendCommand(0x00);

	SendCommand(0xA4); // Normal Display Mode

	SendCommand(0xA8); // Set Multiplex Ratio
	SendCommand(0x7F); // 128 MUX

	SendCommand(0xAB); // Function Selection A
	SendCommand(0x01); // Enable internal VDD regulator

	SendCommand(0xB1); // Set Phase Length
	SendCommand(0xF1);

	SendCommand(0xB3); // Set Front Clock Divider / Oscillator Frequency
	SendCommand(0x00);

	SendCommand(0xB6); // Set Second Pre-charge Period
	SendCommand(0x0F);

	SendCommand(0xBC); // Set Pre-charge Voltage
	SendCommand(0x08);

	SendCommand(0xBE); // Set VCOMH
	SendCommand(0x07);

	SendCommand(0xBF); // Set VSL
	SendCommand(0x02); // Enable internal VSL

	SendCommand(0xAF); // Display ON

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
	if (x >= m_nWidth || y >= m_nHeight)
		return;

	u16 nIndex = (y * (m_nWidth / 8)) + (x / 8);
	u8  nBit   = 7 - (x % 8);

	if (bOn)
		m_pBuffer[nIndex] |= (1 << nBit);
	else
		m_pBuffer[nIndex] &= ~(1 << nBit);
}

bool CSSD1327::GetPixel(u8 x, u8 y) const
{
	if (x >= m_nWidth || y >= m_nHeight)
		return false;

	u16 nIndex = (y * (m_nWidth / 8)) + (x / 8);
	u8  nBit   = 7 - (x % 8);
	return (m_pBuffer[nIndex] & (1 << nBit)) != 0;
}

void CSSD1327::Flip()
{
	// Reset pointers to start of display
	SendCommand(0x15); SendCommand(0x00); SendCommand(0x7F);
	SendCommand(0x75); SendCommand(0x00); SendCommand(0x7F);

	// Convert 1-bit buffer to 4-bit nibbles and send row by row
	for (u8 y = 0; y < m_nHeight; y++)
	{
		u8 row[64]; // 128 pixels / 2 pixels per byte
		for (u8 x = 0; x < 64; x++)
		{
			// Map monochrome to 4-bit grayscale (0x0 or 0xF)
			u8 p1 = GetPixel(x * 2,     y) ? 0xF0 : 0x00;
			u8 p2 = GetPixel(x * 2 + 1, y) ? 0x0F : 0x00;
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
		u8 nCharacter = *pText++;
		for (u8 i = 0; i < CHAR_WIDTH; i++)
		{
			u8 nLine = Font[nCharacter][i];
			for (u8 j = 0; j < CHAR_HEIGHT; j++)
			{
				bool bOn = (nLine >> j) & 0x01;
				SetPixel(x + i, y + j, bInverted ? !bOn : bOn);
			}
		}
		x += CHAR_WIDTH;
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