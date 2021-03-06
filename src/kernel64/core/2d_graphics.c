#include "2d_graphics.h"
#include "vbe.h"
#include "../fonts/fonts.h"
#include "../utils/util.h"

inline Color k_changeColorBrightness(Color color, int r, int g, int b) {
	int red, green, blue;

	red = GETR(color);
	green = GETG(color);
	blue = GETB(color);

	red += r;
	green += g;
	blue += b;

	if (red < 0) {
		red = 0;

	} else if (red > 255) {
		red = 255;
	}

	if (green < 0) {
		green = 0;

	} else if (green > 255) {
		green = 255;
	}

	if (blue < 0) {
		blue = 0;

	} else if (blue > 255) {
		blue = 255;
	}

	return RGB(red, green, blue);
}

inline Color k_changeColorBrightness2(Color color, int r, int g, int b) {
	int red, green, blue;

	red = GETR(color);
	green = GETG(color);
	blue = GETB(color);

	red += r;
	green += g;
	blue += b;

	if (red < 86) {
		red = 86;

	} else if (red > 229) {
		red = 229;
	}

	if (green < 86) {
		green = 86;

	} else if (green > 229) {
		green = 229;
	}

	if (blue < 86) {
		blue = 86;

	} else if (blue > 229) {
		blue = 229;
	}

	return RGB(red, green, blue);
}

inline void k_setRect(Rect* rect, int x1, int y1, int x2, int y2) {
	// This logic guarantee the rule that rect.x1 < rect.x2.
	if (x1 < x2) {
		rect->x1 = x1;
		rect->x2 = x2;

	} else {
		rect->x1 = x2;
		rect->x2 = x1;		
	}

	// This logic guarantee the rule that rect.y1 < rect.y2.
	if (y1 < y2) {
		rect->y1 = y1;
		rect->y2 = y2;

	} else {
		rect->y1 = y2;
		rect->y2 = y1;		
	}
}

inline int k_getRectWidth(const Rect* rect) {
	int width;

	width = rect->x2 - rect->x1 + 1;
	if (width < 0) {
		return -width;
	}

	return width;
}

inline int k_getRectHeight(const Rect* rect) {
	int height;

	height = rect->y2 - rect->y1 + 1;
	if (height < 0) {
		return -height;
	}

	return height;
}

inline bool k_isPointInRect(const Rect* rect, int x, int y) {
	// If (x, y) is outside rectangle, return false.
	if ((x < rect->x1) || (x > rect->x2) || (y < rect->y1) || (y > rect->y2)) {
		return false;
	}

	return true;
}

inline bool k_isRectOverlapped(const Rect* rect1, const Rect* rect2) {
	// If rectangle1 and rectangle2 are not overlapped, return false.
	if ((rect1->x1 > rect2->x2) || (rect1->x2 < rect2->x1) || (rect1->y1 > rect2->y2) || (rect1->y2 < rect2->y1)) {
		return false;
	}

	return true;
}

inline bool k_getOverlappedRect(const Rect* rect1, const Rect* rect2, Rect* overRect) {
	int maxX1;
	int minX2;
	int maxY1;
	int minY2;

	maxX1 = MAX(rect1->x1, rect2->x1);
	minX2 = MIN(rect1->x2, rect2->x2);
	// If rectangle1 and rectangle2 are not overlapped, return false.
	if (maxX1 > minX2) {
		return false;
	}

	maxY1 = MAX(rect1->y1, rect2->y1);
	minY2 = MIN(rect1->y2, rect2->y2);
	// If rectangle1 and rectangle2 are not overlapped, return false.
	if (maxY1 > minY2) {
		return false;
	}

	// If rectangle1 and rectangle2 are overlapped, set overlapped rectangle and return true.
	overRect->x1 = maxX1;
	overRect->y1 = maxY1;
	overRect->x2 = minX2;
	overRect->y2 = minY2;

	return true;
}

inline void k_setCircle(Circle* circle, int x, int y, int radius) {
	circle->x = x;
	circle->y = y;
	circle->radius = radius;
}

inline bool k_isPointInCircle(const Circle* circle, int x, int y) {
	if (((x - circle->x) * (x - circle->x) + (y - circle->y) * (y - circle->y)) <= (circle->radius * circle->radius)) {
		return true;
	}

	return false;
}

inline void __k_drawPixel(Color* outBuffer, const Rect* area, int x, int y, Color color) {
	int width;

	// process clipping.
	if (k_isPointInRect(area, x, y) == false) {
		return;
	}

	width = k_getRectWidth(area);

	*(outBuffer + (y * width) + x) = color;
}

/**
  < Bresenham Line Algorithm >
  - select the next pixel which is the closest one to the line,
    using the way that checks if the accumulated error is more than 0.5 pixel.
  - optimize operations by replacing float, multiplication, division operation with addition, subtraction, shift operation.
      --------------------------------------------------
      error = (dy/dx)*n >= 0.5
              dy*n >= 0.5*dx
              2*dy*n >= dx
      2*dx*error = error' = 2*dy*n >= dx
      --------------------------------------------------
      error = error - 1.0 = (dy/dx)*n - 1.0
      dx*error = dy*n - dx
      2*dx*error = 2*dy*n - 2*dx
      error' = error' - 2*dx
      --------------------------------------------------
*/
void __k_drawLine(Color* outBuffer, const Rect* area, int x1, int y1, int x2, int y2, Color color) {
	int deltaX, deltaY;
	int error = 0;
	int deltaError;
	int x, y; // x, y to draw line.
	int stepX, stepY;
	Rect lineArea;

	// process clipping.
	k_setRect(&lineArea, x1, y1, x2, y2);
	if (k_isRectOverlapped(area, &lineArea) == false) {
		return;
	}

	deltaX = x2 - x1;
	deltaY = y2 - y1;
	
	if (deltaX < 0) {
		deltaX = -deltaX;
		stepX = -1;
		
	} else {
		stepX = 1;
	}
	
	if (deltaY < 0) {
		deltaY = -deltaY;
		stepY = -1;
		
	} else {
		stepY = 1;
	}
	
	// If deltaX > deltaY, draw line based on x-axis.
	if (deltaX > deltaY) {
		deltaError = deltaY << 1; // 2*dy
		y = y1;
		for (x = x1; x != x2; x += stepX) {
			__k_drawPixel(outBuffer, area, x, y, color);
			
			error += deltaError;
			if (error >= deltaX) { // error' = 2*dy*n >= dx
				y += stepY;
				error -= deltaX << 1; // error' = error' - 2*dx
			}
		}
		
		__k_drawPixel(outBuffer, area, x, y, color);
		
	// If deltaX <= deltaY, draw line based on y-axis.
	} else {
		deltaError = deltaX << 1; // 2*dx
		x = x1;
		for (y = y1; y != y2; y += stepY) {
			__k_drawPixel(outBuffer, area, x, y, color);
			
			error += deltaError;
			if (error >= deltaY) { // error' = 2*dx*n >= dy
				x += stepX;
				error -= deltaY << 1; // error' = error' - 2*dy
			}
		}
		
		__k_drawPixel(outBuffer, area, x, y, color);
	}
}

void __k_drawRect(Color* outBuffer, const Rect* area, int x1, int y1, int x2, int y2, Color color, bool fill) {
	int temp;
	int y; // y to draw rectangle.
	Rect drawRect;
	Rect overArea;
	int areaWidth;
	int overWidth;
	
	if (fill == false) {
		// draw 4 lines (edges) of rectangle.
		__k_drawLine(outBuffer, area, x1, y1, x2, y1, color);
		__k_drawLine(outBuffer, area, x1, y1, x1, y2, color);
		__k_drawLine(outBuffer, area, x2, y1, x2, y2, color);
		__k_drawLine(outBuffer, area, x1, y2, x2, y2, color);
		
	} else {
		// process clipping.
		k_setRect(&drawRect, x1, y1, x2, y2);
		if (k_getOverlappedRect(area, &drawRect, &overArea) == false) {
			return;
		}
		
		overWidth = k_getRectWidth(&overArea);
		areaWidth = k_getRectWidth(area);
				
		outBuffer += overArea.y1 * areaWidth + overArea.x1;
		
		for (y = overArea.y1; y < overArea.y2; y++) {
			k_memsetWord(outBuffer, color, overWidth);
			outBuffer += areaWidth;
		}
		
		k_memsetWord(outBuffer, color, overWidth);
	}
}

/**
  < Midpoint Circle Algorithm >
  - select the next pixel which is the closest one to the circle,
    using the way that checks if the midpoint of two candidate pixels is inside circle or outside circle.
  - calculate only 1/8 (45 degree) of circle, and draw remaining 7/8 of circle using symmetry of circle.
  - calculate circle with origin (0, 0), and move it to origin (x, y).
  - optimize operations by replacing float, multiplication, division operation with addition, subtraction, shift operation.
      --------------------------------------------------
      first pixel:     (0, r)     first distance:     d_first
      last pixel:      (x-1, y),  last distance:      d_old
      current pixel 1: (x, y),    current distance 1: d_new1
      current pixel 2: (x, y-1),  current distance 2: d_new2
      --------------------------------------------------
      d_first = 0^2 + (r-0.5)^2 - r^2
              = 0^2 + r^2 - 2*r*0.5 + 0.25 - r^2
              = -r + 0.25
              = -r  // discard 0.25 in integer operation
      
      d_old = (x-1)^2 + (y-0.5)^2 - r^2
      d_new1 = x^2 + (y-0.5)^2 - r^2
      d_new2 = x^2 + (y-1.5)^2 - r^2
      --------------------------------------------------
      d_new1 = x^2 + (y-0.5)^2 - r^2
             = ((x-1)+1)^2 + (y-0.5)^2 - r^2
             = (x-1)^2 + 2*(x-1) + 1 + (y-0.5)^2 - r^2
             = (x-1)^2 + (y-0.5)^2 - r^2 + 2*(x-1) + 1
             = d_old + 2*x - 1
      --------------------------------------------------
      d_new2 = x^2 + (y-1.5)^2 - r^2
             = x^2 + ((y-0.5)-1)^2 - r^2
             = x^2 + (y-0.5)^2 - 2*(y-0.5) + 1 - r^2
             = x^2 + (y-0.5)^2 - r^2 - 2*(y-0.5) + 1
             = d_new1 - 2*y + 2
      --------------------------------------------------
*/

void __k_drawCircle(Color* outBuffer, const Rect* area, int x, int y, int radius, Color color, bool fill) {
	int circleX, circleY; // x, y to draw circle.
	int distance; // distance difference
	
	if (radius < 0) {
		return;
	}
	
	// start drawing circle from (0, r)
	circleY = radius;
	
	if (fill == false) {
		// draw 4 points contacting with x-axis and y-axis.
		__k_drawPixel(outBuffer, area, 0 + x, radius + y, color);
		__k_drawPixel(outBuffer, area, 0 + x, -radius + y, color);
		__k_drawPixel(outBuffer, area, radius + x, 0 + y, color);
		__k_drawPixel(outBuffer, area, -radius + x, 0 + y, color);
		
	} else {
		// draw 2 lines matching x-axis and y-axis.
		__k_drawLine(outBuffer, area, 0 + x, radius + y, 0 + x, -radius + y, color);
		__k_drawLine(outBuffer, area, radius + x, 0 + y, -radius + x, 0 + y, color);
	}
	
	distance = -radius; // d_first = -r  // discard 0.25 in integer operation.
	
	/**
	  calculate only 1/8 (45 degree) of circle, and draw remaining 7/8 of circle using symmetry of circle.
	  calculate circle with origin (0, 0), and move it to origin (x, y).
	*/
	for (circleX = 1; circleX <= circleY; circleX++) {
		distance += (circleX << 1) - 1; // d_new1 = d_old + 2*x - 1
		
		// If the midpoint is outside circle, select the lower pixel out of two candidate pixels.
		// add equal(=) to the condition in oder to cover discarding 0.25.
		if (distance >= 0) {
			circleY--;
			distance += - (circleY << 1) + 2; // d_new2 = d_new1 - 2*y + 2
		}
		
		if (fill == false) {
			// draw 8 points in the symmetric position of (circleX, circleY).
			__k_drawPixel(outBuffer, area, circleX + x, circleY + y, color);
			__k_drawPixel(outBuffer, area, circleX + x, -circleY + y, color);
			__k_drawPixel(outBuffer, area, -circleX + x, circleY + y, color);
			__k_drawPixel(outBuffer, area, -circleX + x, -circleY + y, color);
			__k_drawPixel(outBuffer, area, circleY + x, circleX + y, color);
			__k_drawPixel(outBuffer, area, circleY + x, -circleX + y, color);
			__k_drawPixel(outBuffer, area, -circleY + x, circleX + y, color);
			__k_drawPixel(outBuffer, area, -circleY + x, -circleX + y, color);
			
		} else {
			// draw 4 parallel lines of x-axis in the symmetric position.
			// (use k_drawRect instead of k_drawLine, because it's faster when drawing a parallel line of x-axis.)
			__k_drawRect(outBuffer, area, -circleX + x, circleY + y, circleX + x, circleY + y, color, true);
			__k_drawRect(outBuffer, area, -circleX + x, -circleY + y, circleX + x, -circleY + y, color, true);
			__k_drawRect(outBuffer, area, -circleY + x, circleX + y, circleY + x, circleX + y, color, true);
			__k_drawRect(outBuffer, area, -circleY + x, -circleX + y, circleY + x, -circleX + y, color, true);
		}
	}
}

void __k_drawText(Color* outBuffer, const Rect* area, int x, int y, Color textColor, Color backgroundColor, const char* str, int len) {
	int currentX, currentY; // x, y to draw text.
	int i, j, k;
	byte bitmap;
	byte currentBitmask;
	int bitmapIndex;
	int areaWidth;
	Rect fontArea;
	Rect overArea;
	int startXOffset;
	int startYOffset;
	int overWidth;
	int overHeight;
	
	// initialize current x.
	currentX = x;

	areaWidth = k_getRectWidth(area);

	// draw text (string).
	for (i = 0; i < len; i++) {
		// initialize current y.
		currentY = y * areaWidth;
		
		// process clipping.
		k_setRect(&fontArea, currentX, y, currentX + FONT_DEFAULT_WIDTH - 1, y + FONT_DEFAULT_HEIGHT - 1);
		if (k_getOverlappedRect(area, &fontArea, &overArea) == false) {
			// move to the next character.
			currentX += FONT_DEFAULT_WIDTH;
			continue;
		}

		// Default font data has width * height bits per a character,
		// and it has same order as ASCII code.
		// Thus, bitmap index indicates a byte (8 bits) of current character in the font data.
		bitmapIndex = str[i] * FONT_DEFAULT_HEIGHT;
		
		startXOffset = overArea.x1 - currentX;
		startYOffset = overArea.y1 - y;
		overWidth = k_getRectWidth(&overArea);
		overHeight = k_getRectHeight(&overArea);

		bitmapIndex += startYOffset;

		// draw a character.
		for (j = startYOffset; j < overHeight; j++) {
			bitmap = FONT_DEFAULT_BITMAP[bitmapIndex++];
			currentBitmask = 0x01 << (FONT_DEFAULT_WIDTH - 1 - startXOffset);
			
			// draw a line in a character.
			for (k = startXOffset; k < overWidth; k++) {
				// If a bit in bitmap == 1, draw a pixel with text color.
				if (bitmap & currentBitmask) {
					outBuffer[currentY + currentX + k] = textColor;
					
				// If a bit in bitmap == 0, draw a pixel with backgroud color.
				} else {
					outBuffer[currentY + currentX + k] = backgroundColor;
				}

				currentBitmask = currentBitmask >> 1;
			}
			
			// move to the next line in a character.
			currentY += areaWidth;
		}
		
		// move to the next character.
		currentX += FONT_DEFAULT_WIDTH;
	}
}
