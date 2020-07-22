/*
 * BADGE.TEAM framebuffer driver
 * Uses parts of the Adafruit GFX Arduino libray
 */

/*
This is the core graphics library for all our displays, providing a common
set of graphics primitives (points, lines, circles, etc.).  It needs to be
paired with a hardware-specific library for each display device we carry
(to handle the lower-level functions).

Adafruit invests time and resources providing this open source code, please
support Adafruit & open-source hardware by purchasing products from Adafruit!

Copyright (c) 2013 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
 */

#include "include/driver_framebuffer_internal.h"

#include <math.h>

#define TAG "fb-drawing"

#ifdef CONFIG_DRIVER_FRAMEBUFFER_ENABLE

typedef struct texture_2d_t {
	uint32_t *buffer;
	int16_t width;
	int16_t height;
} texture_2d;

typedef struct point_3d_t {
	double x, y, z;
} point_3d;

typedef struct triangle_3d_t {
	bool modificationAllowed; //This is false if the shape is being drawn, because at this time it will not affect anything to modify vertices.
	point_3d *point0;
	point_3d *point1;
	point_3d *point2;
	double u0, v0;
	double u1, v1;
	double u2, v2;
} triangle_3d;

/*
 * Defines a shader applicable to 2D drawing.
 * A shader is a piece of code which allows for special coloring of a drawn object in an otherwise impossible way.
 * 
 * Arguments:
 * uint32_t tint -- Tint of the drawing, named tint because this would be used to tint the texture, or specify the color if no texture is present.
 * texture_2d *texture -- Pointer to the texture object, if NULL then there is no texture.
 * int16_t screenX -- Real X position on screen.
 * int16_t screenY -- Real Y position on screen.
 * double preTransformX -- X position on screen that this would have been it if it were untransformed.
 * double preTransformY -- Y position on screen that this would have been it if it were untransformed.
 * double u -- Represents the X coordinate on the texture, from 0 to 1.
 * double v -- Represents the Y coordinate on the texture, from 0 to 1.
 * void *args -- Anything as an argument to the shader.
 * int n_args -- How many arguments the shader has recieved.
 * 
 * Returns:
 * The color to be drawn, formatted in 0xAARRGGBB.
 */
typedef uint32_t(*shader_2d)(uint32_t tint, texture_2d *texture, int16_t screenX, int16_t screenY, \
				double preTransformX, double preTransformY, double u, double v, void *args, int n_args);

/*
 * Defines a shader applicable to 2D drawing.
 * A shader is a piece of code which allows for special coloring of a drawn object in an otherwise impossible way.
 * 3D shaders also have the power to distort an object, such as moving vertices around.
 * For this reason, the shader will be called twice:
 * Once to displace the vertices,
 * And once more to draw the shape or model.
 * 
 * Arguments:
 * uint32_t tint -- Tint of the drawing, named tint because this would be used to tint the texture, or specify the color if no texture is present.
 * texture_2d *texture -- Pointer to the texture object, if NULL then there is no texture.
 * int16_t screenX -- Real X position on screen.
 * int16_t screenY -- Real Y position on screen.
 * double preTransformX -- X position in space that this would have been it if it were untransformed.
 * double preTransformY -- Y position in space that this would have been it if it were untransformed.
 * double preTransformZ -- Z position in space that this would have been it if it were untransformed.
 * double u -- Represents the X coordinate on the texture, from 0 to 1.
 * double v -- Represents the Y coordinate on the texture, from 0 to 1.
 * triangle_3d *triangle -- The triangle upon which this shader is applied, it is a pointer to allow modifiction of the triangle.
 * void *args -- Anything as an argument to the shader.
 * int n_args -- How many arguments the shader has recieved.
 * 
 * Returns:
 * The color to be drawn, formatted in 0xAARRGGBB.
 */
typedef uint32_t(*shader_3d)(uint32_t tint, texture_2d *texture, int16_t screenX, int16_t screenY, \
				double preTransformX, double preTransformY, double preTransformZ, double u, double v, triangle_3d *triangle, void *args, int n_args);

#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }

void driver_framebuffer_line(Window* window, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color)
{
	int16_t steep = abs(y1 - y0) > abs(x1 - x0);
	if (steep) {
		_swap_int16_t(x0, y0);
		_swap_int16_t(x1, y1);
	}

	if (x0 > x1) {
		_swap_int16_t(x0, x1);
		_swap_int16_t(y0, y1);
	}

	int16_t dx, dy;
	dx = x1 - x0;
	dy = abs(y1 - y0);

	int16_t err = dx / 2;
	int16_t ystep;

	if (y0 < y1) {
		ystep = 1;
	} else {
		ystep = -1;
	}

	for (/*empty*/; x0<=x1; x0++) {
		if (steep) {
			driver_framebuffer_setPixel(window, y0, x0, color);
		} else {
			driver_framebuffer_setPixel(window, x0, y0, color);
		}
		err -= dy;
		if (err < 0) {
			y0 += ystep;
			err += dx;
		}
	}
}

// void driver_framebuffer_triangle0(Window* window, double x0, double y0, double x1, double y1, double x2, double y2, uint32_t color)
// {
// 	//sort the points such that point 0 is the top and point 2 is the bottom
// 	//lower number is higher on screen
// 	float temp;
// 	if (y1 < y0) { //ensure y1 is under y0
// 		//swap points 1 and 0
// 		temp = y0;
// 		y0 = y1;
// 		y1 = temp;
// 		temp = x0;
// 		x0 = x1;
// 		x1 = temp;
// 	}
// 	if (y2 < y1) { //ensure y2 is under y1
// 		//swap points 2 and 1
// 		temp = y1;
// 		y1 = y2;
// 		y2 = temp;
// 		temp = x1;
// 		x1 = x2;
// 		x2 = temp;
// 	}
// 	if (y2 < y0) { //ensure y2 is under y0
// 		//swap points 2 and 0
// 		temp = y0;
// 		y0 = y2;
// 		y2 = temp;
// 		temp = x0;
// 		x0 = x2;
// 		x2 = temp;
// 	}
// 	if (y1 < y0) { //ensure y1 is under y0 once more
// 		//swap points 1 and 0
// 		temp = y0;
// 		y0 = y1;
// 		y1 = temp;
// 		temp = x0;
// 		x0 = x1;
// 		x1 = temp;
// 	}
// 	double yDist = y1 - y0; //between points 0 and 1
// 	int nSteps = (int) (yDist + 1); //between points 0 and 1
// 	double yStep = yDist / (double) nSteps; //between points 0 and 1
// 	double xMiddle = x0 + (x2 - x0) / (y2 - y0) * (y1 - y0);
// 	double xStep0 = (xMiddle - x0) / (double) nSteps; //between points 0 and 2
// 	double xStep1 = (x1 - x0) / (double) nSteps; //between points 0 and 1
// 	//"top" part of the triangle
// 	for (int i = 0; i < nSteps; i++) { //go along the rows
// 		int y = (int) (y0 + yStep * i + 0.5);
// 		int xC0 = (int) (x0 + xStep0 * i + 0.5);
// 		int xC1 = (int) (x0 + xStep1 * i + 0.5);
// 		int nXSteps = xC0 - xC1;
// 		int xStepMult = -1;
// 		if (nXSteps < 0) {
// 			nXSteps = -nXSteps;
// 			xStepMult = 1;
// 		}
// 		for (int j = 0; j < nXSteps; j++) { //and plot each pixel that falls in it
// 			driver_framebuffer_setPixel(window, xC0 + j * xStepMult, y, color);
// 		}
// 	}
// 	yDist = y2 - y1; //between points 1 and 2
// 	nSteps = (int) (yDist + 1); //between points 1 and 2
// 	yStep = yDist / (double) nSteps + 0.01; //between points 1 and 2
// 	xStep0 = (x2 - xMiddle) / (double) nSteps; //between points 0 and 2
// 	xStep1 = (x2 - x1) / (double) nSteps; //between points 1 and 2
// 	//"bottom" part of the triangle
// 	for (int i = 0; i < nSteps; i++) { //go along the rows
// 		int y = (int) (y1 + yStep * i + 0.5);
// 		int xC0 = (int) (xMiddle + xStep0 * i + 0.5);
// 		int xC1 = (int) (x1 + xStep1 * i + 0.5);
// 		int nXSteps = xC0 - xC1;
// 		int xStepMult = -1;
// 		if (nXSteps < 0) {
// 			nXSteps = -nXSteps;
// 			xStepMult = 1;
// 		}
// 		for (int j = 0; j < nXSteps; j++) { //and plot each pixel that falls in it
// 			driver_framebuffer_setPixel(window, xC0 + j * xStepMult, y, color);
// 		}
// 	}
// }
// void driver_framebuffer_triangle1(Window* window, double x0, double y0, double x1, double y1, double x2, double y2, uint32_t color)
// {
// 	//sort the points such that point 0 is the top and point 2 is the bottom
// 	//lower number is higher on screen
// 	float temp;
// 	if (y1 < y0) { //ensure y1 is under y0
// 		//swap points 1 and 0
// 		temp = y0;
// 		y0 = y1;
// 		y1 = temp;
// 		temp = x0;
// 		x0 = x1;
// 		x1 = temp;
// 	}
// 	if (y2 < y1) { //ensure y2 is under y1
// 		//swap points 2 and 1
// 		temp = y1;
// 		y1 = y2;
// 		y2 = temp;
// 		temp = x1;
// 		x1 = x2;
// 		x2 = temp;
// 	}
// 	if (y2 < y0) { //ensure y2 is under y0
// 		//swap points 2 and 0
// 		temp = y0;
// 		y0 = y2;
// 		y2 = temp;
// 		temp = x0;
// 		x0 = x2;
// 		x2 = temp;
// 	}
// 	if (y1 < y0) { //ensure y1 is under y0 once more
// 		//swap points 1 and 0
// 		temp = y0;
// 		y0 = y1;
// 		y1 = temp;
// 		temp = x0;
// 		x0 = x1;
// 		x1 = temp;
// 	}	
// 	double yDist = y1 - y0; //between points 0 and 1
// 	int nSteps = (int) (yDist + 1); //between points 0 and 1
// 	double yStep = yDist / (double) nSteps; //between points 0 and 1
// 	double xMiddle = x0 + (x2 - x0) / (y2 - y0) * (y1 - y0);
// 	double xStep0 = (xMiddle - x0) / (double) nSteps; //between points 0 and 2
// 	double xStep1 = (x1 - x0) / (double) nSteps; //between points 0 and 1
// 	//"top" part of the triangle
// 	for (int i = 0; i < nSteps; i++) { //go along the rows
// 		int y = (int) (y0 + yStep * i + 0.5);
// 		double xStart = x0 + xStep0 * i;
// 		double xEnd = x0 + xStep1 * i;
// 		double mult = 1;
// 		int nHorSteps = (int) (xEnd - xStart);
// 		if (nHorSteps < 0) {
// 			nHorSteps = -nHorSteps;
// 		}
// 		nHorSteps ++;
// 		double xStep = (xEnd - xStart) / nHorSteps * mult;
// 		for (int j = 0; j < nHorSteps; j++) {
// 			driver_framebuffer_setPixel(window, (int) (xStart + xStep * (double) j + 0.5), y, color);
// 		}
// 	}
// 	yDist = y2 - y1; //between points 1 and 2
// 	nSteps = (int) (yDist + 1); //between points 1 and 2
// 	yStep = yDist / (double) nSteps; //between points 1 and 2
// 	xStep0 = (x2 - xMiddle) / (double) nSteps; //between points 0 and 2
// 	xStep1 = (x2 - x1) / (double) nSteps; //between points 1 and 2
// 	//"bottom" part of the triangle
// 	for (int i = 0; i < nSteps; i++) { //go along the rows
// 		int y = (int) (y1 + yStep * i + 0.5);
// 		double xStart = xMiddle + xStep0 * i;
// 		double xEnd = x1 + xStep1 * i;
// 		double mult = 1;
// 		int nHorSteps = (int) (xEnd - xStart);
// 		if (nHorSteps < 0) {
// 			nHorSteps = -nHorSteps;
// 		}
// 		nHorSteps ++;
// 		double xStep = (xEnd - xStart) / nHorSteps * mult;
// 		for (int j = 0; j < nHorSteps; j++) {
// 			driver_framebuffer_setPixel(window, (int) (xStart + xStep * (double) j + 0.5), y, color);
// 		}
// 	}
// }
// void clamp_to_view(Window* window, int *x, int *y)
// {
// 	int16_t width;
// 	int16_t height;
// 	driver_framebuffer_window_getSize(window, &width, &height);
// 	if (x[0] < 0) {
// 		x[0] = 0;
// 	}
// 	else if (x[0] >= width) {
// 		x[0] = width - 1;
// 	}
// 	if (y[0] < 0) {
// 		y[0] = 0;
// 	}
// 	else if (y[0] >= height) {
// 		y[0] = height - 1;
// 	}
// }
// void driver_framebuffer_triangle2(Window* window, double x0, double y0, double x1, double y1, double x2, double y2, uint32_t color)
// {
// 	//sort the points such that point 0 is the top and point 2 is the bottom
// 	//lower number is higher on screen
// 	float temp;
// 	if (y1 < y0) { //ensure y1 is under y0
// 		//swap points 1 and 0
// 		temp = y0;
// 		y0 = y1;
// 		y1 = temp;
// 		temp = x0;
// 		x0 = x1;
// 		x1 = temp;
// 	}
// 	if (y2 < y1) { //ensure y2 is under y1
// 		//swap points 2 and 1
// 		temp = y1;
// 		y1 = y2;
// 		y2 = temp;
// 		temp = x1;
// 		x1 = x2;
// 		x2 = temp;
// 	}
// 	if (y2 < y0) { //ensure y2 is under y0
// 		//swap points 2 and 0
// 		temp = y0;
// 		y0 = y2;
// 		y2 = temp;
// 		temp = x0;
// 		x0 = x2;
// 		x2 = temp;
// 	}
// 	if (y1 < y0) { //ensure y1 is under y0 once more
// 		//swap points 1 and 0
// 		temp = y0;
// 		y0 = y1;
// 		y1 = temp;
// 		temp = x0;
// 		x0 = x1;
// 		x1 = temp;
// 	}	
// 	// Get the bounding box of the triangle
// 	int leftMost = (int) (x0 - 1);
// 	int rightMost = (int) (x0 + 1);
// 	if (x1 - 1 < leftMost) {
// 		leftMost = (int) (x1 - 1);
// 	}
// 	if (x2 - 1 < leftMost) {
// 		leftMost = (int) (x2 - 1);
// 	}
// 	if (x1 + 1 > rightMost) {
// 		rightMost = (int) (x1 + 1);
// 	}
// 	if (x2 + 1 > rightMost) {
// 		rightMost = (int) (x2 + 1);
// 	}
// 	int topMost = (int) (y0);
// 	int bottomMost = (int) (y2 + 1);
// 	// Clamp the box to the view area
// 	clamp_to_view(window, &leftMost, &topMost);
// 	clamp_to_view(window, &rightMost, &bottomMost);
// 	double xStep0 = (x1 - x0) / (y1 - y0);
// 	double xStep1 = (x2 - x1) / (y2 - y1);
// 	double xStep2 = (x2 - x0) / (y2 - y0);
// 	// Plot all the points in the box which fall in the triangle
// 	for (int y = topMost; y <= bottomMost; y++) {
// 		for (int x = leftMost; x <= rightMost; x++) {
// 			bool doDraw = 0;
// 			if (y > y1 && y <= y2) {
// 				double xc0 = x1 + xStep1 * (y - y1);
// 				double xc1 = x0 + xStep2 * (y - y0);
// 				if (xc0 > xc1) {
// 					double tmp = xc0;
// 					xc0 = xc1;
// 					xc1 = tmp;
// 				}
// 				doDraw = x >= xc0 && x < xc1;
// 			}
// 			else if (y <= y1 && y >= y0) {
// 				double xc0 = x0 + xStep0 * (y - y0);
// 				double xc1 = x0 + xStep2 * (y - y0);
// 				if (xc0 > xc1) {
// 					double tmp = xc0;
// 					xc0 = xc1;
// 					xc1 = tmp;
// 				}
// 				doDraw = x >= xc0 && x < xc1;
// 			}
// 			if (doDraw) {
// 				driver_framebuffer_setPixel(window, x, y, color);
// 			}
// 		}
// 	}
// }
// void driver_framebuffer_triangle3(Window* window, double x0, double y0, double x1, double y1, double x2, double y2, uint32_t color)
// {
// 	//sort the points such that point 0 is the top and point 2 is the bottom
// 	//lower number is higher on screen
// 	float temp;
// 	if (y1 < y0) { //ensure y1 is under y0
// 		//swap points 1 and 0
// 		temp = y0;
// 		y0 = y1;
// 		y1 = temp;
// 		temp = x0;
// 		x0 = x1;
// 		x1 = temp;
// 	}
// 	if (y2 < y1) { //ensure y2 is under y1
// 		//swap points 2 and 1
// 		temp = y1;
// 		y1 = y2;
// 		y2 = temp;
// 		temp = x1;
// 		x1 = x2;
// 		x2 = temp;
// 	}
// 	if (y2 < y0) { //ensure y2 is under y0
// 		//swap points 2 and 0
// 		temp = y0;
// 		y0 = y2;
// 		y2 = temp;
// 		temp = x0;
// 		x0 = x2;
// 		x2 = temp;
// 	}
// 	if (y1 < y0) { //ensure y1 is under y0 once more
// 		//swap points 1 and 0
// 		temp = y0;
// 		y0 = y1;
// 		y1 = temp;
// 		temp = x0;
// 		x0 = x1;
// 		x1 = temp;
// 	}	
// 	// Get the bounding box of the triangle
// 	// int leftMost = (int) (x0 - 1);
// 	// int rightMost = (int) (x0 + 1);
// 	// if (x1 - 1 < leftMost) {
// 	// 	leftMost = (int) (x1 - 1);
// 	// }
// 	// if (x2 - 1 < leftMost) {
// 	// 	leftMost = (int) (x2 - 1);
// 	// }
// 	// if (x1 + 1 > rightMost) {
// 	// 	rightMost = (int) (x1 + 1);
// 	// }
// 	// if (x2 + 1 > rightMost) {
// 	// 	rightMost = (int) (x2 + 1);
// 	// }
// 	int topMost = (int) y0;
// 	if (topMost <= y0 - 0.5) topMost ++;
// 	int bottomMost = (int) y2;
// 	if (bottomMost <= y2 - 0.5) bottomMost ++;
// 	// Clamp the box to the view area
// 	//clamp_to_view(window, &leftMost, &topMost);
// 	//clamp_to_view(window, &rightMost, &bottomMost);

// 	double xStep0 = (x1 - x0) / (y1 - y0 + 1);
// 	double xStep1 = (x2 - x1) / (y2 - y1 + 1);
// 	double xStep2 = (x2 - x0) / (y2 - y0 + 1);
// 	// Plot all the points in the box which fall in the triangle
// 	int y = topMost;
// 	for (y = topMost; y <= y1; y++) {
// 		double xc0 = x0 + xStep0 * (y - y0) + 0.5;
// 		double xc1 = x0 + xStep2 * (y - y0) + 0.5;
// 		if (xc0 > xc1) {
// 			double tmp = xc0;
// 			xc0 = xc1;
// 			xc1 = tmp;
// 		}
// 		int max = (int) xc1;
// 		for (int x = (int) xc0; x <= xc1; x++) {
// 			driver_framebuffer_setPixel(window, x, y, color);
// 		}
// 	};
// 	for (; y <= bottomMost; y++) {
// 		double xc0 = x1 + xStep1 * (y - y1) + 0.5;
// 		double xc1 = x0 + xStep2 * (y - y0) + 0.5;
// 		if (xc0 > xc1) {
// 			double tmp = xc0;
// 			xc0 = xc1;
// 			xc1 = tmp;
// 		}
// 		int max = (int) xc1;
// 		for (int x = (int) xc0; x <= xc1; x++) {
// 			driver_framebuffer_setPixel(window, x, y, color);
// 		}
// 	}
// }

void driver_framebuffer_triangle(Window* window, double x0, double y0, double x1, double y1, double x2, double y2, uint32_t color)
{
	//sort the points such that point 0 is the top and point 2 is the bottom
	//lower number is higher on screen
	float temp;
	if (y1 < y0) { //ensure y1 is under y0
		//swap points 1 and 0
		temp = y0;
		y0 = y1;
		y1 = temp;
		temp = x0;
		x0 = x1;
		x1 = temp;
	}
	if (y2 < y1) { //ensure y2 is under y1
		//swap points 2 and 1
		temp = y1;
		y1 = y2;
		y2 = temp;
		temp = x1;
		x1 = x2;
		x2 = temp;
	}
	if (y2 < y0) { //ensure y2 is under y0
		//swap points 2 and 0
		temp = y0;
		y0 = y2;
		y2 = temp;
		temp = x0;
		x0 = x2;
		x2 = temp;
	}
	if (y1 < y0) { //ensure y1 is under y0 once more
		//swap points 1 and 0
		temp = y0;
		y0 = y1;
		y1 = temp;
		temp = x0;
		x0 = x1;
		x1 = temp;
	}
	
	// From point 0 to point 1
	double inc01 = (x1 - x0) / (y1 - y0);
	double add01 = (x0 / inc01 - y0) * inc01;
	if (inc01 == 0) add01 = x0;
	// From point 0 to point 2
	double inc02 = (x2 - x0) / (y2 - y0);
	double add02 = (x0 / inc02 - y0) * inc02;
	if (inc02 == 0) add02 = x0;
	// From point 1 to point 2
	double inc12 = (x2 - x1) / (y2 - y1);
	double add12 = (x1 / inc12 - y1) * inc12;
	if (inc12 == 0) add12 = x1;

	// if (inc01 > 1000000 || inc02 > 1000000 || inc12 > 1000000 || inc01 < -1000000 || inc02 < -1000000 || inc12 < -1000000) {
	// 	printf("%f + %f, %f + %f, %f + %f, (%f : %f) (%f : %f) (%f : %f)\n", inc01, add01, inc02, add02, inc12, add12, x0, y0, x1, y1, x2, y2);
	// 	//return;
	// }

	// Check whether we need to draw the top part
	int yCheck = (int) (y0 + 0.5);
	if ((double) yCheck + 0.5 <= y1) {
		// Draw top part
		int startY = (int) (y0 + 0.5);
		int endY = (int) (y1 + 0.5);
		for (int y = startY; y < endY; y++) {
			int startX = ((double) y + 0.5) * inc01 + add01 + 0.5;
			int endX = ((double) y + 0.5) * inc02 + add02 + 0.5;
			if (startX > endX) {
				int tmp = startX;
				startX = endX;
				endX = tmp;
			}
			for (int x = startX; x < endX; x++) {
				driver_framebuffer_setPixel(window, x, y, color);
			}
		}
	}
	// Check whether we need to draw the bottom part
	yCheck = (int) (y1 + 0.5);
	if ((double) yCheck + 0.5 <= y2) {
		// Draw bottom part
		int startY = (int) (y1 + 0.5);
		int endY = (int) (y2 + 0.5);
		for (int y = startY; y < endY; y++) {
			int startX = ((double) y + 0.5) * inc12 + add12 + 0.5;
			int endX = ((double) y + 0.5) * inc02 + add02 + 0.5;
			if (startX > endX) {
				int tmp = startX;
				startX = endX;
				endX = tmp;
			}
			for (int x = startX; x < endX; x++) {
				driver_framebuffer_setPixel(window, x, y, color);
			}
		}
	}
}

void driver_framebuffer_quad(Window* window, double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, uint32_t color)
{
	// This is easier to do if represented as triangles.
	driver_framebuffer_triangle(window, x0, y0, x1, y1, x2, y2, color);
	driver_framebuffer_triangle(window, x0, y0, x2, y2, x3, y3, color);
}

void driver_framebuffer_rect(Window* window, int16_t x, int16_t y, uint16_t w, uint16_t h, bool fill, uint32_t color)
{
	if (fill) {
		for (int16_t i=x; i<x+w; i++) {
			driver_framebuffer_line(window, i, y, i, y+h-1, color);
		}
	} else {
		driver_framebuffer_line(window, x,    y,     x+w-1, y,     color);
		driver_framebuffer_line(window, x,    y+h-1, x+w-1, y+h-1, color);
		driver_framebuffer_line(window, x,    y,     x,     y+h-1, color);
		driver_framebuffer_line(window, x+w-1,y,     x+w-1, y+h-1, color);
	}
}

// Not actually a unit test
double circle_test_radius(matrix_stack_2d* stack, double radius)
{
	matrix_2d current = stack->current;
	matrix_2d rotation = matrix_2d_rotate(M_PI * 0.25);
	double maxSqr = 0;
	double x = 0;
	double y = radius;
	matrix_2d_transform_point(current, &x, &y);
	double sqrDist = x * x + y * y;
	if (sqrDist > maxSqr) {
		maxSqr = sqrDist;
	}
	current = matrix_2d_multiply(current, rotation);
	maxSqr = 0;
	x = 0;
	y = radius;
	matrix_2d_transform_point(current, &x, &y);
	sqrDist = x * x + y * y;
	if (sqrDist > maxSqr) {
		maxSqr = sqrDist;
	}
	current = matrix_2d_multiply(current, rotation);
	maxSqr = 0;
	x = 0;
	y = radius;
	matrix_2d_transform_point(current, &x, &y);
	sqrDist = x * x + y * y;
	if (sqrDist > maxSqr) {
		maxSqr = sqrDist;
	}
	return sqrt(maxSqr);
}

void driver_framebuffer_circle(Window* window, matrix_stack_2d* stack, double x, double y, double radius, double startAngle, double endAngle, bool fill, uint32_t color)
{
	// Test the scale of the stack so as to have enough precision to fool the viewer
	double effectiveCircumfrence = circle_test_radius(stack, radius) * M_PI;
	int nSteps = effectiveCircumfrence < 60 ? (int) (effectiveCircumfrence / 1.7) : 40;
	double anglePerStep = (startAngle - endAngle) / (double) nSteps;
	// Make a copy of the matrix for later use
	matrix_2d current = stack->current;
	// Apply this multiple times instead of slow sin/cos
	matrix_2d rotationStep = matrix_2d_rotate(anglePerStep);
	current = matrix_2d_multiply(current, matrix_2d_translate(x, y));
	if (startAngle > 0.0000001) {
		// Rotate to the starting angle
		current = matrix_2d_multiply(current, matrix_2d_rotate(startAngle));
	}
	matrix_2d_transform_point(stack->current, &x, &y);
	// Start circling!
	if (fill) {
		double lastX = 0;
		double lastY = -radius;
		matrix_2d_transform_point(current, &lastX, &lastY);
		for (int i = 0; i < nSteps; i++) {
			double newX = 0;
			double newY = -radius;
			current = matrix_2d_multiply(current, rotationStep);
			matrix_2d_transform_point(current, &newX, &newY);
			driver_framebuffer_triangle(window, x, y, lastX, lastY, newX, newY, color);
			lastX = newX;
			lastY = newY;
		}
	}
	else
	{
		double lastX = 0;
		double lastY = -radius;
		matrix_2d_transform_point(current, &lastX, &lastY);
		for (int i = 0; i < nSteps; i++) {
			double newX = 0;
			double newY = -radius;
			current = matrix_2d_multiply(current, rotationStep);
			matrix_2d_transform_point(current, &newX, &newY);
			driver_framebuffer_line(window, (int) (lastX + 0.5), (int) (lastY + 0.5), (int) (newX + 0.5), (int) (newY + 0.5), color);
			lastX = newX;
			lastY = newY;
		}
	}
}

void driver_framebuffer_circle_old(Window* window, int16_t x0, int16_t y0, uint16_t r, uint16_t startAngle, uint16_t endAngle, bool fill, uint32_t color)
{
	bool havePrevPixel = 0;
	int prevX = 0;
	int prevY = 0;
	if (startAngle >= endAngle) return;
	for (int f=(fill ? 0 : r); f <= r; f++) {
		havePrevPixel = false;
		for (int i=startAngle; i<endAngle; i++) {
			double radians = i * M_PI / 180;
			int px = x0 + f * cos(radians);
			int py = y0 + f * sin(radians);
			if (havePrevPixel && ((prevX != px) || (prevY != py))) {
				driver_framebuffer_line(window, prevX, prevY, px, py, color);
			} else {
				driver_framebuffer_setPixel(window, px, py, color);
			}
			prevX = px;
			prevY = py;
			havePrevPixel = true;
		}
	}
}

#endif
