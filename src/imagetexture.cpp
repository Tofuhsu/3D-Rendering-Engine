#include "imagetexture.h"

#include <stb_image.h>

ImageTexture::ImageTexture(const std::string filePath)
	: texFilePath(filePath)
{
	imageWidth = 0;
	imageHeight = 0;
	numChannels = 0;
	textureObj = 0;

	// Decode to tightly packed RGBA8. Images store the top row first while
	// OpenGL expects the bottom row first, so flip on load. HDR files (.hdr)
	// are converted to 8-bit by stb_image (gamma 2.2, values above 1 clip).
	stbi_set_flip_vertically_on_load(1);
	int fileChannels = 0;
	unsigned char* pixels = stbi_load(texFilePath.c_str(), &imageWidth, &imageHeight, &fileChannels, 4);
	if (pixels == nullptr) {
		std::cerr << "[ERROR] Failed to load image texture: " << filePath
				  << " (" << stbi_failure_reason() << ")" << std::endl;
		imageWidth = imageHeight = 0;
		return;
	}
	numChannels = fileChannels;

	glGenTextures(1, &textureObj);
	glBindTexture(GL_TEXTURE_2D, textureObj);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageWidth, imageHeight,
					0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	stbi_image_free(pixels);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glGenerateMipmap(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, 0);
}

ImageTexture::~ImageTexture()
{
	glDeleteTextures(1, &textureObj);
}

void ImageTexture::Bind(GLenum textureUnit)
{
	glActiveTexture(textureUnit);
	glBindTexture(GL_TEXTURE_2D, textureObj);
}
