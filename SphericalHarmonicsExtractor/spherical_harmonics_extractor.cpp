
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <iostream>
#include <fstream>
#include <string>
#include <array>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <spdlog/spdlog.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <glm/glm.hpp>

#define Pi 3.14159265359

namespace glm {
void to_json(json &j, const glm::vec3 &P) { j = {{"x", P.x}, {"y", P.y}, {"z", P.z}}; };

void from_json(const json &j, glm::vec3 &P) {
	P.x = j.at("x").get<double>();
	P.y = j.at("y").get<double>();
	P.z = j.at("z").get<double>();
}

inline void to_json(json &j, const glm::mat4 &v) {
	j = {v[0][0], v[0][1], v[0][2], v[0][3], v[1][0], v[1][1], v[1][2], v[1][3], v[2][0], v[2][1], v[2][2], v[2][3], v[3][0], v[3][1], v[3][2], v[3][3]};
}

inline void from_json(const json &j, glm::mat4 &v) {
	v[0][0] = j.at(0);
	v[0][1] = j.at(1);
	v[0][2] = j.at(2);
	v[0][3] = j.at(3);
	v[1][0] = j.at(4);
	v[1][1] = j.at(5);
	v[1][2] = j.at(6);
	v[1][3] = j.at(7);
	v[2][0] = j.at(8);
	v[2][1] = j.at(9);
	v[2][2] = j.at(10);
	v[2][3] = j.at(11);
	v[3][0] = j.at(12);
	v[3][1] = j.at(13);
	v[3][2] = j.at(14);
	v[3][3] = j.at(15);
}
} // namespace glm

void ToMatrix(std::array<std::array<float, 3>, 9> &coeffs, std::array<glm::mat4, 3> &matrices) {
	//Form the quadratic form matrix (see equations 11 and 12 in paper)

	float c1 = 0.429043f;
	float c2 = 0.511664f;
	float c3 = 0.743125f;
	float c4 = 0.886227f;
	float c5 = 0.247708f;

	for (int col = 0; col < 3; ++col) {
		// Equation 12

		matrices[col][0][0] = c1 * coeffs[8][col]; /* c1 L_{22}  */
		matrices[col][0][1] = c1 * coeffs[4][col]; /* c1 L_{2-2} */
		matrices[col][0][2] = c1 * coeffs[7][col]; /* c1 L_{21}  */
		matrices[col][0][3] = c2 * coeffs[3][col]; /* c2 L_{11}  */

		matrices[col][1][0] = c1 * coeffs[4][col]; /* c1 L_{2-2} */
		matrices[col][1][1] = -c1 * coeffs[8][col]; /*-c1 L_{22}  */
		matrices[col][1][2] = c1 * coeffs[5][col]; /* c1 L_{2-1} */
		matrices[col][1][3] = c2 * coeffs[1][col]; /* c2 L_{1-1} */

		matrices[col][2][0] = c1 * coeffs[7][col]; /* c1 L_{21}  */
		matrices[col][2][1] = c1 * coeffs[5][col]; /* c1 L_{2-1} */
		matrices[col][2][2] = c3 * coeffs[6][col]; /* c3 L_{20}  */
		matrices[col][2][3] = c2 * coeffs[2][col]; /* c2 L_{10}  */

		matrices[col][3][0] = c2 * coeffs[3][col]; /* c2 L_{11}  */
		matrices[col][3][1] = c2 * coeffs[1][col]; /* c2 L_{1-1} */
		matrices[col][3][2] = c2 * coeffs[2][col]; /* c2 L_{10}  */
		matrices[col][3][3] = c4 * coeffs[0][col] - c5 * coeffs[6][col]; /* c4 L_{00} - c5 L_{20} */
	}
}

void UpdateCoeffs(std::array<std::array<float, 3>, 9> &coeffs, const float *rgb, float domega, float x, float y, float z) {
	for (int col = 0; col < 3; ++col) {
		float hdr_val = rgb[col];
		if (hdr_val < 0)
			hdr_val = 0;
		float c = 0.282095;
		coeffs[0][col] += hdr_val * c * domega;

		c = 0.488603;
		coeffs[1][col] += hdr_val * (c * y) * domega; // Y_{1-1} = 0.488603 y
		coeffs[2][col] += hdr_val * (c * z) * domega; // Y_{10}  = 0.488603 z
		coeffs[3][col] += hdr_val * (c * x) * domega; // Y_{11}  = 0.488603 x

		// The Quadratic terms, L_{2m} -2 <= m <= 2

		// First, L_{2-2}, L_{2-1}, L_{21} corresponding to xy,yz,xz
		c = 1.092548;

		coeffs[4][col] += hdr_val * (c * x * y) * domega; // Y_{2-2} = 1.092548 xy
		coeffs[5][col] += hdr_val * (c * y * z) * domega; // Y_{2-1} = 1.092548 yz
		coeffs[7][col] += hdr_val * (c * x * z) * domega; // Y_{21}  = 1.092548 xz

		// L_{20}.  Note that Y_{20} = 0.315392 (3z^2 - 1)
		c = 0.315392;
		coeffs[6][col] += hdr_val * (c * (3 * z * z - 1)) * domega;

		// L_{22}.  Note that Y_{22} = 0.546274 (x^2 - y^2)
		c = 0.546274;
		coeffs[8][col] += hdr_val * (c * (x * x - y * y)) * domega;
	}
}


//def sinc(x):
//	"""Supporting sinc function
//	"""
//	if (abs(x) < 1.0e-4):
//		return 1.0
//	else:
//		return (math.sin(x) / x)
// for probe
// def _prefilter(hdr, width, height):
//    for i in range(width):
//        for j in range(height):
//            v = (width/2.0 - i) / (width/2.0) // u ranges from -1 to 1 */
//            u = (j-width/2.0)/(width/2.0)     // u ranges from -1 to 1 */
//            r = math.sqrt(u*u+v*v)            // The "radius"

//            if r > 1.0:
//                // Consider only circle with r<1
//                continue

//            from math import sin, cos, atan2

//            theta = Pi*r ;       // theta parameter of (i,j)
//            phi = atan2(v,u) ;        // phi parameter

//            x = sin(theta)*cos(phi)   // Cartesian components
//            y = sin(theta)*sin(phi)
//            z = cos(theta)

//            """
//            Computation of the solid angle.  This follows from some
//            elementary calculus converting sin(theta) d theta d phi
//            into coordinates in terms of r.  This calculation should
//            be redone if the form of the input changes
//            """

//            domega = (2*Pi/width)*(2*Pi/width)*sinc(theta)

//            updatecoeffs(hdr[i, j], domega, x, y, z) // Update Integration

void ProcessSphericalHarmonics(const float *data, const int &width, const int &height, const std::string &out) {
	std::array<std::array<float, 3>, 9> coeffs = {
		{
			{0, 0, 0}, {0, 0, 0}, {0, 0, 0},
			{0, 0, 0}, {0, 0, 0}, {0, 0, 0},
			{0, 0, 0}, {0, 0, 0}, {0, 0, 0}
		}
	};

	for (float i = 0; i < width; ++i) {
		spdlog::debug(fmt::format("Computing {}%\r", int(i / width * 100)));
		for (float j = 0; j < height; ++j) {
			float theta = Pi * (j / height); // theta parameter of (i,j)
			float phi = (2 * Pi) * (i / width); // phi parameter

			float x = sin(theta) * cos(phi); // Cartesian components
			float y = sin(theta) * sin(phi);
			float z = cos(theta);

			//Computation of the solid angle.This follows from some
			//elementary calculus converting sin(theta) d theta d phi
			//into coordinates in terms of r.This calculation should
			//be redone if the form of the input changes


			float domega = (2 * Pi / width) * (Pi / height) * sin(theta);

			UpdateCoeffs(coeffs, &data[(int)((i + j * width) * 3)], domega, x, y, z); // Update Integration
		}
	}

	// save coeff to out file
	nlohmann::json js;
	js["L00"] = glm::vec3(coeffs[0][2], coeffs[0][1], coeffs[0][0]);
	js["L11"] = glm::vec3(coeffs[3][2], coeffs[3][1], coeffs[3][0]);
	js["L10"] = glm::vec3(coeffs[2][2], coeffs[2][1], coeffs[2][0]);
	js["L1_1"] = glm::vec3(coeffs[1][2], coeffs[1][1], coeffs[1][0]);
	js["L21"] = glm::vec3(coeffs[7][2], coeffs[7][1], coeffs[7][0]);
	js["L2_1"] = glm::vec3(coeffs[5][2], coeffs[5][1], coeffs[5][0]);
	js["L2_2"] = glm::vec3(coeffs[4][2], coeffs[4][1], coeffs[4][0]);
	js["L20"] = glm::vec3(coeffs[6][2], coeffs[6][1], coeffs[6][0]);
	js["L22"] = glm::vec3(coeffs[8][2], coeffs[8][1], coeffs[8][0]);


	std::array<glm::mat4, 3> matrices = {glm::mat4(1), glm::mat4(1), glm::mat4(1)};
	ToMatrix(coeffs, matrices);

	js["red"] = matrices[0];
	js["green"] = matrices[1];
	js["blue"] = matrices[2];

	std::ofstream file(out);
	file << js;
	file.close();
}


int main(int narg, const char **args) {
	std::cout << "Spherical Harmonics Extractor 1.0" << std::endl;

	if (narg > 3) {
		spdlog::info("Need args\nArg1: input (picture path)\nArg2: output (json)");
		return -1;
	}

	std::string out(args[2]);

	int texWidth, texHeight, texChannels;
	float *pixels = stbi_loadf(args[1], &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	if (!pixels) {
		spdlog::error("Can't open the input path picture");
		return -1;
	}

	ProcessSphericalHarmonics(pixels, texWidth, texHeight, out);

	stbi_image_free(pixels);
	return 0;
}
