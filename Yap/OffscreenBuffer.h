/* @@@LICENSE
*
*      Copyright (c) 2012 Hewlett-Packard Development Company, L.P.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
LICENSE@@@ */

#ifndef OFFSCREENBUFFER_H
#define OFFSCREENBUFFER_H

class ProcessMutex;


class OffscreenBuffer
{
public:

	OffscreenBuffer(int width, int height);
	OffscreenBuffer(int key);
	~OffscreenBuffer();

	void getDimensions(int& width, int& height);
	void getContentRect(int& cx, int& cy, int& cw, int& ch);

	int key() const;
	
	// These are meant to be changed only from the server
	void viewportSizeChanged(int w, int h);
	void contentsSizeChanged(int w, int h);
	bool scrollChanged(int& x, int& y);
	bool scrollAndContentsChanged(int x, int y, int w, int h);
	void copyFromBuffer(uint32_t* srcBuffer, int srcStride,
						int srcPositionX, int srcPositionY,
						int srcSizeWidth, int srcSizeHeight);
	void copyFromBuffer(uint32_t* srcBuffer, int srcStride,
						int srcPositionX, int srcPositionY,
						int srcSizeWidth, int srcSizeHeight,
						int newScrollX, int newScrollY);

	// To be used from the client side
	void copyToBuffer(uint32_t* dstBuffer, int dstStride,
					  int dstPositionX, int dstPositionY,
					  int dstSizeWidth, int dstSizeHeight);
	void scaleToBuffer(uint32_t* dstBuffer, int dstStride,
					   int dstLeft, int dstTop, int dstRight, int dstBottom,
					   int srcLeft, int srcTop, int srcRight, int srcBottom,
					   double scaleFactor);

	int writeToFile(FILE* pFile, int left, int top, int right, int bottom) const;

	void dump(const char* fileName);
	void erase(void);
	
private:

	// assumes mutex is locked
	void invalidate();

	struct BufferInfo {
		int bufferId;
		int bufferWidth;
		int bufferHeight;

		int viewportWidth;
		int viewportHeight;
		int contentsWidth;
		int contentsHeight;
		
		int scrollX;
		int scrollY;
		
		int width;
		int height;		
		int stride;
		int xPadding;
		int yPadding;
	};

	ProcessMutex* m_mutex;
	uint32_t* m_buffer;
	uint32_t m_bufferSize;
};

#endif /* OFFSCREENBUFFER_H */
