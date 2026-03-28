#ifndef _ssd1327_h
#define _ssd1327_h

#include "lcd/lcd.h"
#include "lcd/drivers/ssd1306.h"
#include <circle/i2cmaster.h>

class CSSD1327 : public CLCD
{
public:
	using TLCDRotation = CSSD1306::TLCDRotation;
	using TLCDMirror   = CSSD1306::TLCDMirror;

	CSSD1327(CI2CMaster* pI2CMaster, u8 nAddress, u8 nWidth, u8 nHeight, TLCDRotation Rotation, TLCDMirror Mirror);
	virtual ~CSSD1327();

	bool Initialize() override;
	void Clear(bool bUpdate = false) override;
	void Flip() override;

	// Graphical methods
	void Print(const char* pText, u8 x, u8 y, bool bInverted = false, bool bUpdate = false) override;
	void DrawImage(TImage Image, bool bUpdate = false) override;
	void DrawFilledRect(u8 x1, u8 y1, u8 x2, u8 y2, bool bUpdate = false) override;

	TType GetType() const override { return TType::Graphical; }

private:
	void WriteCommand(u8 nCommand) const;
	void WriteCommand(const u8* pData, size_t nSize) const;
	void SendData(const u8* pData, size_t nSize);

	// Buffer management
	void SetPixel(u8 x, u8 y, bool bOn);
	bool GetPixel(u8 x, u8 y) const;

	CI2CMaster*  m_pI2CMaster;
	u8           m_nAddress;
	TLCDRotation m_Rotation;
	TLCDMirror   m_Mirror;
	
	// Internal 1-bit buffer (128x128 = 16384 bits = 2048 bytes)
	u8*          m_pBuffer;
	size_t       m_nBufferSize;
};

#endif