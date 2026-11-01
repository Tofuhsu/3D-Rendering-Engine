#include "imagetexture.h"

#ifdef __APPLE__
#include <ImageIO/ImageIO.h>

// Decode an image file to tightly packed RGBA8, bottom row first (OpenGL convention).
static bool LoadImageRGBA(const std::string& path, std::vector<unsigned char>& pixels, int& w, int& h)
{
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr,
		(const UInt8*)path.c_str(), (CFIndex)path.size(), false);
	CGImageSourceRef src = url ? CGImageSourceCreateWithURL(url, nullptr) : nullptr;
	CGImageRef img = src ? CGImageSourceCreateImageAtIndex(src, 0, nullptr) : nullptr;
	if (url) CFRelease(url);
	if (src) CFRelease(src);
	if (!img) return false;

	w = (int)CGImageGetWidth(img);
	h = (int)CGImageGetHeight(img);
	pixels.assign((size_t)w * h * 4, 0);
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	CGContextRef ctx = CGBitmapContextCreate(pixels.data(), w, h, 8, (size_t)w * 4, cs,
		kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
	// Flip vertically so the first row in memory is the bottom of the image.
	CGContextTranslateCTM(ctx, 0, h);
	CGContextScaleCTM(ctx, 1, -1);
	CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
	CGContextRelease(ctx);
	CGColorSpaceRelease(cs);
	CGImageRelease(img);
	return true;
}
#endif

ImageTexture::ImageTexture(const std::string filePath)
	: texFilePath(filePath)
{
	imageWidth = 0;
	imageHeight = 0;
	numChannels = 0;
	textureObj = 0;

#ifdef __APPLE__
	std::vector<unsigned char> pixels;
	if (!LoadImageRGBA(texFilePath, pixels, imageWidth, imageHeight)) {
		std::cerr << "[ERROR] Failed to load image texture: " << filePath << std::endl;
		return;
	}
	numChannels = 4;
	glGenTextures(1, &textureObj);
	glBindTexture(GL_TEXTURE_2D, textureObj);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageWidth, imageHeight,
					0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
#else
	// Try to load texture image.
	texImage = cv::imread(texFilePath);
	if (texImage.rows == 0 || texImage.cols == 0) {
		std::cerr << "[ERROR] Failed to load image texture: " << filePath << std::endl;
		return;
	}
	imageWidth = texImage.cols;
	imageHeight = texImage.rows;
	numChannels = texImage.channels();

	// Flip texture in vertical direction.
	// OpenCV has smaller y coordinate on top; while OpenGL has larger.
	cv::flip(texImage, texImage, 0);

	glGenTextures(1, &textureObj);
    glBindTexture(GL_TEXTURE_2D, textureObj);
    switch (numChannels) {
	case 1:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, imageWidth, imageHeight, 
						0, GL_RED, GL_UNSIGNED_BYTE, texImage.ptr());
		break;
	case 3:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imageWidth, imageHeight, 
						0, GL_BGR, GL_UNSIGNED_BYTE, texImage.ptr());
		break;
	case 4:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageWidth, imageHeight, 
						0, GL_BGRA, GL_UNSIGNED_BYTE, texImage.ptr());
		break;
	default:
		std::cerr << "[ERROR] Unsupport texture format" << std::endl;
		break;
	}
#endif

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	
	glGenerateMipmap(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, 0);
}

ImageTexture::~ImageTexture()
{
	glDeleteTextures(1, &textureObj);
#ifndef __APPLE__
	texImage.release();
#endif
}

void ImageTexture::Bind(GLenum textureUnit)
{
	glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_2D, textureObj);
}

void ImageTexture::Preview()
{
#ifdef __APPLE__
	std::cerr << "[WARN] Texture preview needs OpenCV; not available on macOS build." << std::endl;
#else
	std::string windowText = "[DEBUG] TexturePreview: " + texFilePath;
	cv::Mat previewImg = cv::Mat(texImage.rows, texImage.cols, texImage.type());
	cv::cvtColor(texImage, previewImg, cv::COLOR_BGR2RGB);
	cv::imshow(windowText, previewImg);
	cv::waitKey(0);
#endif
}
