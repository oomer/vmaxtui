#pragma once

// This is a set of common oomer utilities for Vmax models
// Will avoid using bella_sdk

// Standard C++ library includes - these provide essential functionality
#include <map>          // For key-value pair data structures (maps)
#include <set>          // For set data structure
#include <vector>       // For dynamic arrays (vectors)
#include <string>       // For std::string
#include <cstdint>      // For fixed-size integer types (uint8_t, uint32_t, etc.)
#include <fstream>      // For file operations (reading/writing files)
#include <iostream>     // For input/output operations (cout, cin, etc.)
#include <filesystem>   // For file system operations (directory handling, path manipulation)

#include "../lzfse/src/lzfse.h"
#include "../libplist/include/plist/plist.h" // Library for handling Apple property list files
#include "thirdparty/json.hpp"

using json = nlohmann::json;

// Define STB_IMAGE_IMPLEMENTATION before including to create the implementation
#define STB_IMAGE_IMPLEMENTATION
#include "thirdparty/stb_image.h" // STB Image library

// Structure to represent a 4x4 matrix for 3D transformations
// The matrix is stored as a 2D array where m[i][j] represents row i, column j
struct VmaxMatrix4x4 {
    double m[4][4];
    // Constructor: Creates an identity matrix (1's on diagonal, 0's elsewhere)
    VmaxMatrix4x4() {
        for(int i = 0; i < 4; i++) {
            for(int j = 0; j < 4; j++) {
                m[i][j] = (i == j) ? 1.0 : 0.0;  // 1.0 on diagonal, 0.0 elsewhere
            }
        }
    }
    
    // Matrix multiplication operator to combine transformations
    // Returns: A new matrix that represents the combined transformation
    VmaxMatrix4x4 operator*(const VmaxMatrix4x4& other) const {
        VmaxMatrix4x4 result;
        for(int i = 0; i < 4; i++) {
            for(int j = 0; j < 4; j++) {
                result.m[i][j] = 0.0;
                for(int k = 0; k < 4; k++) {
                    result.m[i][j] += m[i][k] * other.m[k][j];
                }
            }
        }
        return result;
    }
    
    // Helper function to create a translation matrix
    static VmaxMatrix4x4 createTranslation(double x, double y, double z) {
        VmaxMatrix4x4 result;
        result.m[3][0] = x;  // Translation in X (bottom row)
        result.m[3][1] = y;  // Translation in Y (bottom row)
        result.m[3][2] = z;  // Translation in Z (bottom row)
        return result;
    }
    
    // Helper function to create a scale matrix
    static VmaxMatrix4x4 createScale(double x, double y, double z) {
        VmaxMatrix4x4 result;
        result.m[0][0] = x;  // Scale in X
        result.m[1][1] = y;  // Scale in Y
        result.m[2][2] = z;  // Scale in Z
        return result;
    }
};

// Converts axis-angle rotation to a 4x4 rotation matrix
// Parameters:
//   ax, ay, az: The axis vector to rotate around (doesn't need to be normalized)
//   angle: The angle to rotate by (in radians)
// Returns: A 4x4 rotation matrix that can be used to transform vectors
VmaxMatrix4x4 axisAngleToMatrix4x4(double ax, double ay, double az, double angle) {
    // Step 1: Normalize the axis vector to make it a unit vector
    // This is required for the rotation formula to work correctly
    double length = sqrt(ax*ax + ay*ay + az*az);
    if (length != 0) {
        ax /= length;
        ay /= length;
        az /= length;
    }
    
    // Step 2: Calculate trigonometric values needed for the rotation
    double s = sin(angle);  // sine of angle
    double c = cos(angle);  // cosine of angle
    double t = 1.0 - c;     // 1 - cos(angle), used in formula
    
    // Step 3: Create rotation matrix using Rodrigues' rotation formula
    // This formula converts an axis-angle rotation into a 3x3 matrix
    // We'll embed it in the upper-left corner of our 4x4 matrix
    VmaxMatrix4x4 result;
    
    // First row of rotation matrix (upper-left 3x3 portion)
    result.m[0][0] = t*ax*ax + c;      // First column
    result.m[0][1] = t*ax*ay + s*az;   // Second column (changed sign)
    result.m[0][2] = t*ax*az - s*ay;   // Third column (changed sign)
    
    // Second row of rotation matrix
    result.m[1][0] = t*ax*ay - s*az;   // First column (changed sign)
    result.m[1][1] = t*ay*ay + c;      // Second column
    result.m[1][2] = t*ay*az + s*ax;   // Third column (changed sign)
    
    // Third row of rotation matrix
    result.m[2][0] = t*ax*az + s*ay;   // First column (changed sign)
    result.m[2][1] = t*ay*az - s*ax;   // Second column (changed sign)
    result.m[2][2] = t*az*az + c;      // Third column
    
    // Fourth row and column remain unchanged (0,0,0,1)
    // This is already set by the constructor
    return result;
}

// Combine a rotation, translation, and scale into a single 4x4 matrix
// Parameters:
//   rotx, roty, rotz: The axis vector to rotate around (doesn't need to be normalized)
//   rota: The angle to rotate by (in radians)
//   posx, posy, posz: The position to translate to
//   scalex, scaley, scalez: The scale to apply to the object
// Returns: A 4x4 matrix that represents the combined transformation
VmaxMatrix4x4 combineVmaxTransforms(double rotx, double roty, double rotz, double rota, double posx, double posy, double posz, double scalex, double scaley, double scalez) {
    VmaxMatrix4x4 rotMat4 = axisAngleToMatrix4x4(rotx, 
                                                 roty, 
                                                 rotz, 
                                                 rota);
    VmaxMatrix4x4 transMat4 = VmaxMatrix4x4();
    transMat4 = transMat4.createTranslation(posx, 
                                            posy, 
                                            posz);
    VmaxMatrix4x4 scaleMat4 = VmaxMatrix4x4();
    scaleMat4 = scaleMat4.createScale(scalex, 
                                      scaley, 
                                      scalez);
    VmaxMatrix4x4 resultMat4 = scaleMat4 * rotMat4 * transMat4;
    return resultMat4;
}

struct VmaxRGBA {
    uint8_t r, g, b, a;
};

// Read a 256x1 PNG file and return a vector of VmaxRGBA colors
std::vector<VmaxRGBA> read256x1PaletteFromPNG(const std::string& filename) {
    int width, height, channels;
    // Load the image with 4 desired channels (RGBA)
    unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 4);
    
    if (!data) {
        std::cerr << "Error loading PNG file: " << filename << std::endl;
        return {};
    }
    // Make sure the image is 256x1 as expected
    if (width != 256 || height != 1) {
        std::cerr << "Warning: Expected a 256x1 image, but got " << width << "x" << height << std::endl;
    }
    // Create our palette array
    std::vector<VmaxRGBA> palette;
    // Read each pixel (each pixel is 4 bytes - RGBA)
    for (int i = 0; i < width; i++) {
        VmaxRGBA color;
        color.r = data[i * 4];
        color.g = data[i * 4 + 1];
        color.b = data[i * 4 + 2];
        color.a = data[i * 4 + 3];
        palette.push_back(color);
    }
    stbi_image_free(data); // Free the image data
    return palette;
}

// Standard useful voxel structure, maps easily to VoxelMax's voxel structure and probably MagicaVoxel's
// We are using this to unpack a chunked voxel into a simple giant voxel
// using a uint8_t saves memory over a uint32_t and both VM and MV models are 256x256x256
// The world itself can be larger, see scene.json
// Helper struct because in VmaxModel we use array of arrays to store material and color
// Allowing us to group voxels by material and color
struct VmaxVoxel {
    uint8_t x, y, z;
    uint8_t material;    // material value 0-7
    uint8_t palette;     // Color palette mapping index 0-255, search palette1.png
    uint16_t chunkID;       // Chunk ID 0-511 8x8x8 is a morton code
    uint16_t minMorton;     // morton encoded offset from chunk origin 0-32767 32x32x32, decoded value is added to voxel x y z
    // Constructor
    VmaxVoxel(      uint8_t _x,
                    uint8_t _y,
                    uint8_t _z,
                    uint8_t _material,
                    uint8_t _palette,
                    uint16_t _chunkID,
                    uint16_t _minMorton)
                    : x(_x), y(_y), z(_z), material(_material), palette(_palette), chunkID(_chunkID), minMorton(_minMorton) {
    }
};

inline uint32_t compactBits(uint32_t n) {
    // For a 32-bit integer in C++
    n &= 0x49249249;                     // Keep only every 3rd bit
    n = (n ^ (n >> 2)) & 0xc30c30c3;     // Merge groups
    n = (n ^ (n >> 4)) & 0x0f00f00f;     // Continue merging
    n = (n ^ (n >> 8)) & 0x00ff00ff;     // Merge larger groups
    n = (n ^ (n >> 16)) & 0x0000ffff;    // Final merge
    return n;
}

// Optimized function to decode Morton code using parallel bit manipulation
inline void decodeMorton3DOptimized(uint32_t morton, uint32_t& x, uint32_t& y, uint32_t& z) {
    x = compactBits(morton);
    y = compactBits(morton >> 1);
    z = compactBits(morton >> 2);
}

struct VmaxMaterial {
    std::string materialName;
    double transmission;
    double roughness;
    double metalness;
    double emission;
    bool enableShadows;
    bool dielectric; // future use
    bool volumetric; // future use
};

// Create a structure to represent a model with its voxels with helper functions
struct VmaxModel {
    // Model identifier or name
    std::string vmaxbFileName; // file name is used like a key
    
    // Voxels organized by material and color
    // First dimension: material (0-7)
    // Second dimension: color (1-255, index 0 unused since color 0 means no voxel)
    std::vector<VmaxVoxel> voxels[8][256];
    
    // Each model has local 0-7 materials
    std::array<VmaxMaterial, 8> materials;
    // Each model has local colors
    std::array<VmaxRGBA, 256> colors;

    // Constructor
    VmaxModel(const std::string& modelName) : vmaxbFileName(modelName) {
    }
    
    // Add a voxel to this model
    void addVoxel(int x, int y, int z, int material, int color, int chunk, int chunkMin) {
        if (material >= 0 && material < 8 && color > 0 && color < 256) {
            voxels[material][color].emplace_back(x, y, z, material, color, chunk, chunkMin);
        }
    }

    // Add a materials to this model
    void addMaterials(const std::array<VmaxMaterial, 8> newMaterials) {
        materials = newMaterials;
    }
    
    // Add a colors to this model
    void addColors(const std::array<VmaxRGBA, 256> newColors) {
        colors = newColors;
    }
     
    // Get all voxels of a specific material and color
    const std::vector<VmaxVoxel>& getVoxels(int material, int color) const {
        if (material >= 0 && material < 8 && color > 0 && color < 256) {
            return voxels[material][color];
        }
        static std::vector<VmaxVoxel> empty;
        return empty;
    }
    
    // Get total voxel count for this model
    size_t getTotalVoxelCount() const {
        size_t count = 0;
        for (int m = 0; m < 8; m++) {
            for (int c = 1; c < 256; c++) {  // Skip index 0
                count += voxels[m][c].size();
            }
        }
        return count;
    }

    // Get a map of used materials and their associated colors
    std::map<int, std::set<int>> getUsedMaterialsAndColors() const {
        std::map<int, std::set<int>> result;
        
        // Iterate through fixed size arrays - we know it's 8 materials and 256 colors
        for (int material = 0; material < 8; material++) {
            for (int color = 1; color < 256; color++) {  // Skip index 0 as it means no voxel
                if (!voxels[material][color].empty()) {
                    result[material].insert(color);
                }
            }
        }
        return result;
    }
};

inline std::array<VmaxMaterial, 8> getVmaxMaterials(plist_t pnodPalettePlist) {
    // Directly access the materials array
    std::array<VmaxMaterial, 8> vmaxMaterials;
    plist_t materialsNode = plist_dict_get_item(pnodPalettePlist, "materials");
    if (materialsNode && plist_get_node_type(materialsNode) == PLIST_ARRAY) {
        uint32_t materialsCount = plist_array_get_size(materialsNode);
        //std::cout << "Found materials array with " << materialsCount << " items" << std::endl;
        
        // Process each material
        for (uint32_t i = 0; i < materialsCount; i++) {
            plist_t materialNode = plist_array_get_item(materialsNode, i);
            if (materialNode && plist_get_node_type(materialNode) == PLIST_DICT) {
                plist_t nameNode = plist_dict_get_item(materialNode, "mi");
                std::string vmaxMaterialName;
                double vmaxTransmission = 0.0;  // Declare outside the if block
                double vmaxEmission = 0.0;  // Declare outside the if block
                double vmaxRoughness = 0.0;  // Declare outside the if block
                double vmaxMetalness = 0.0;  // Declare outside the if block
                uint8_t vmaxEnableShadows = 1;
                
                if (nameNode) {
                    char* rawName = nullptr;
                    plist_get_string_val(nameNode, &rawName);
                    vmaxMaterialName = rawName ? rawName : "unnamed";
                    free(rawName);
                }
                plist_t pnodTc = plist_dict_get_item(materialNode, "tc");
                if (pnodTc) { plist_get_real_val(pnodTc, &vmaxTransmission); }
                plist_t pnodEmission = plist_dict_get_item(materialNode, "sic");
                if (pnodEmission) { plist_get_real_val(pnodEmission, &vmaxEmission); }
                plist_t pnodRoughness = plist_dict_get_item(materialNode, "rc");
                if (pnodRoughness) { plist_get_real_val(pnodRoughness, &vmaxRoughness); }
                plist_t pnodMetalness = plist_dict_get_item(materialNode, "mc");
                if (pnodMetalness) { plist_get_real_val(pnodMetalness, &vmaxMetalness); }
                plist_t pnodEnableShadow = plist_dict_get_item(materialNode, "sh");
                if (pnodEnableShadow) { plist_get_bool_val(pnodEnableShadow, &vmaxEnableShadows); }

                vmaxMaterials[i] = {
                    vmaxMaterialName,
                    vmaxTransmission,
                    vmaxRoughness,
                    vmaxMetalness,
                    vmaxEmission,
                    static_cast<bool>(vmaxEnableShadows),
                    false, // dielectric
                    false, // volumetric
                };
            }
        }
    } else {
        std::cout << "No materials array found or invalid type" << std::endl;
    }
    #ifdef _DEBUG
        for (const auto& material : vmaxMaterials) {
            std::cout << "Material: " << material.materialName << std::endl;
            std::cout << "  Transmission: " << material.transmission << std::endl;
            std::cout << "  Emission: " << material.emission << std::endl;
            std::cout << "  Roughness: " << material.roughness << std::endl;
            std::cout << "  Metalness: " << material.metalness << std::endl;
            std::cout << "  Enable Shadows: " << material.enableShadows << std::endl;
            std::cout << "  Dielectric: " << material.dielectric << std::endl;
            std::cout << "  Volumetric: " << material.volumetric << std::endl;
        }
    #endif
    return vmaxMaterials;
}


/**
 * Decodes a voxel's material index and palette index from the ds data stream
 * 
 * @param dsData The raw ds data stream containing material and palette index pairs
 * @param mortonOffset offset to apply to the morton code
 * @param chunkID chunk ID
 * @return vector of VmaxVoxel structures containing the voxels local to a snapshot
 */
inline std::vector<VmaxVoxel> decodeVoxels(const std::vector<uint8_t>& dsData, int mortonOffset, uint16_t chunkID) {
    std::vector<VmaxVoxel> voxels;
    uint8_t material;
    uint8_t color;
    for (int i = 0; i < dsData.size() - 1; i += 2) {
        material = dsData[i]; // also known as a layer color
        color = dsData[i + 1];
        uint32_t _tempx, _tempy, _tempz;
        decodeMorton3DOptimized(i/2 + mortonOffset,
                                _tempx,
                                _tempy,
                                _tempz); // index IS the morton code
        if (color != 0) {
            VmaxVoxel voxel = {
                static_cast<uint8_t>(_tempx), 
                static_cast<uint8_t>(_tempy), 
                static_cast<uint8_t>(_tempz), 
                material,
                color,
                chunkID, // todo is wasteful to pass chunkID?
                static_cast<uint16_t>(mortonOffset)
            };
            voxels.push_back(voxel);
        }
    }
    return voxels;
}

//libplist reads in 64 bits
struct VmaxChunkInfo {
    int64_t id; // was uint but will use -1 to indicate bad chunk
    uint64_t type;
    uint64_t mortoncode;
    uint32_t voxelOffsetX;
    uint32_t voxelOffsetY;
    uint32_t voxelOffsetZ;
};

// Helper function to get a nested dictionary item
// @param root: root dictionary
// @param path: path to the item
// @return: item
// Using a vector of strings for dynamic path length
plist_t getNestedPlistNode(plist_t plist_root, const std::vector<std::string>& path) {
    plist_t current = plist_root;
    for (const auto& key : path) {
        if (!current) return nullptr;
        current = plist_dict_get_item(current, key.c_str());
    }
    return current;
}

// Need morton code in snapshot before we can decode voxels
// @param an individual chunk: plist_t of a snapshot dict->item->s
// @return Chunk level info needed to decode voxels
VmaxChunkInfo vmaxChunkInfo(const plist_t& plist_snapshot_dict_item) {
    uint64_t id;
    uint64_t type;
    uint64_t mortoncode;
    uint32_t voxelOffsetX, voxelOffsetY, voxelOffsetZ;
    try {
        plist_t plist_snapshot = getNestedPlistNode(plist_snapshot_dict_item, {"s"});

        // vmax file format must guarantee the existence 
        // s.st.min
        // s.id.t
        // s.id.c
        plist_t plist_min = getNestedPlistNode(plist_snapshot, {"st", "min"});
        plist_t plist_min_val = plist_array_get_item(plist_min, 3);
        plist_get_uint_val(plist_min_val, &mortoncode);
        
        // convert to 32x32x32 chunk offset
        decodeMorton3DOptimized(mortoncode, 
                                voxelOffsetX, 
                                voxelOffsetY, 
                                voxelOffsetZ); 

        plist_t plist_type = getNestedPlistNode(plist_snapshot, {"id","t"});
        plist_get_uint_val(plist_type, &type);
        plist_t plist_chunk = getNestedPlistNode(plist_snapshot, {"id","c"});
        plist_get_uint_val(plist_chunk, &id);

        return VmaxChunkInfo{static_cast<int64_t>(id), 
                            type,
                            mortoncode,
                            voxelOffsetX,
                            voxelOffsetY,
                            voxelOffsetZ};
    } catch (std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;    
        // Just continue to next snapshot
        // This bypass might mean we miss useful snapshots
    }
    return VmaxChunkInfo{-1, 0, 0, 0, 0, 0};
}


// Right after we get VmaxChunkInfo, we can get the voxels because we need morton chunk offset
// @param pnodSnaphot: plist_t of a snapshot
// @return vector of VmaxVoxel
//std::vector<VmaxVoxel> getVmaxSnapshot(plist_t& pnod_each_snapshot) {
std::vector<VmaxVoxel> vmaxVoxelInfo(plist_t& plist_datastream, uint64_t chunkID, uint64_t minMorton) {
    std::vector<VmaxVoxel> voxelsArray; 
    try {

        // Extract the binary data
        char* data = nullptr;
        uint64_t length = 0;
        plist_get_data_val(plist_datastream, &data, &length);
        auto foo =  std::vector<uint8_t>(data, data + length) ;
        std::vector<VmaxVoxel> allModelVoxels = decodeVoxels(std::vector<uint8_t>(data, data + length), minMorton, chunkID);


        uint32_t model_8x8x8_x, model_8x8x8_y, model_8x8x8_z;
        decodeMorton3DOptimized(chunkID, 
                                model_8x8x8_x, 
                                model_8x8x8_y, 
                                model_8x8x8_z); // index IS the morton code
        int model_256x256x256_x = model_8x8x8_x * 8; // convert to model space
        int model_256x256x256_y = model_8x8x8_y * 8;
        int model_256x256x256_z = model_8x8x8_z * 8;

        for (const VmaxVoxel& eachVmaxVoxel : allModelVoxels) {
            auto [  chunk_32x32x32_x, 
                    chunk_32x32x32_y, 
                    chunk_32x32x32_z, 
                    materialMap, 
                    colorMap, 
                    chunkID, 
                    minMorton] = eachVmaxVoxel;

            int voxel_256x256x256_x = model_256x256x256_x + chunk_32x32x32_x;
            int voxel_256x256x256_y = model_256x256x256_y + chunk_32x32x32_y;
            int voxel_256x256x256_z = model_256x256x256_z + chunk_32x32x32_z;

            auto one_voxel = VmaxVoxel(voxel_256x256x256_x, 
                                    voxel_256x256x256_y, 
                                    voxel_256x256x256_z, 
                                    materialMap, 
                                    colorMap,
                                    chunkID,
                                    minMorton);
            voxelsArray.push_back(one_voxel);
        }
        return voxelsArray;
    } catch (std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;    
        // Just continue to next snapshot
        // This bypass might mean we miss useful snapshots
    }
    return voxelsArray; // empty return
}

/**
 * Read a binary plist file and return a plist node.
 * if the file is lzfse compressed, decompress it and parse the decompressed data
 * 
 * Memory Management:
 * - Creates temporary buffers for decompression
 * - Handles buffer resizing if needed
 * - Returns a plist node that must be freed by the caller
 * 
 * @param lzfseFullName Path to the LZFSE file
 * @param plistName Name of the plist file to write (optional)
 * @return plist_t A pointer to the root node of the parsed plist, or nullptr if failed
 */
// read binary lzfse compressed/uncompressed file 
inline plist_t readPlist(const std::string& inStrPlist, std::string outStrPlist, bool decompress) {
    // Get file size using std::filesystem
    size_t rawFileSize = std::filesystem::file_size(inStrPlist);
    std::vector<uint8_t> rawBytes(rawFileSize);
    std::vector<uint8_t> outBuffer;
    size_t decodedSize = 0;
    if (decompress) { // files are either lzfse compressed or uncompressed
        std::ifstream rawBytesFile(inStrPlist, std::ios::binary);
        if (!rawBytesFile.is_open()) {
            std::cerr << "Error: Could not open plist file: " << inStrPlist << std::endl;
            throw std::runtime_error("Error message"); // [learned] no need to return nullptr
        }
        
        rawBytesFile.read(reinterpret_cast<char*>(rawBytes.data()), rawFileSize);
        rawBytesFile.close();
        // Start with output buffer 4x input size (compression ratio is usually < 4)
        size_t outAllocatedSize = rawFileSize * 8;
        // vector<uint8_t> automatically manages memory allocation/deallocation
        //std::vector<uint8_t> outBuffer(outAllocatedSize);
        outBuffer.resize(outAllocatedSize);  // Resize preserves existing content

        // LZFSE needs a scratch buffer for its internal operations
        // Get the required size and allocate it
        size_t scratchSize = lzfse_decode_scratch_size();
        std::vector<uint8_t> scratch(scratchSize);

        // Decompress the data, growing the output buffer if needed
        //size_t decodedSize = 0;
        while (true) {
            // Try to decompress with current buffer size
            decodedSize = lzfse_decode_buffer(
                outBuffer.data(),     // Where to store decompressed data
                outAllocatedSize,     // Size of output buffer
                rawBytes.data(),            // Source of compressed data
                rawBytes.size(),            // Size of compressed data
                scratch.data());      // Scratch space for LZFSE

            // Check if we need a larger buffer:
            // - decodedSize == 0 indicates failure
            // - decodedSize == outAllocatedSize might mean buffer was too small
            if (decodedSize == 0 || decodedSize == outAllocatedSize) {
                outAllocatedSize *= 2;  // Double the buffer size
                outBuffer.resize(outAllocatedSize);  // Resize preserves existing content
                continue;  // Try again with larger buffer
            }
            break;  // Successfully decompressed
        }

        // Check if decompression failed
        if (decodedSize == 0) {
            std::cerr << "Failed to decompress data" << std::endl;
            return nullptr;
        }

        // If requested, write the decompressed data to a file
        if (!outStrPlist.empty()) {
            std::ofstream outFile(outStrPlist, std::ios::binary);
            if (outFile) {
                outFile.write(reinterpret_cast<const char*>(outBuffer.data()), decodedSize);
                std::cout << "Wrote decompressed plist to: " << outStrPlist << std::endl;
            } else {
                std::cerr << "Failed to write plist to file: " << outStrPlist << std::endl;
            }
        }
    } else {
        outBuffer.resize(rawFileSize); 
        // if the file is not compressed, just read the raw bytes
        std::ifstream rawBytesFile(inStrPlist, std::ios::binary);
        if (!rawBytesFile.is_open()) {
            std::cerr << "Error: Could not open plist file: " << inStrPlist << std::endl;
            throw std::runtime_error("Error message"); // [learned] no need to return nullptr
        }
        rawBytesFile.read(reinterpret_cast<char*>(outBuffer.data()), rawFileSize);
        rawBytesFile.close();
        decodedSize = rawFileSize; // decodedSize is the same as rawFileSize when data is not compressed


    } // outBuffer now contains the raw bytes of the plist file

    // Parse the decompressed data as a plist
    plist_t root_node = nullptr;
    plist_format_t format;  // Will store the format of the plist (binary, xml, etc.)
    
    // Convert the raw decompressed data into a plist structure
    plist_err_t err = plist_from_memory(
        reinterpret_cast<const char*>(outBuffer.data()),  // Cast uint8_t* to char*
        static_cast<uint32_t>(decodedSize),               // Cast size_t to uint32_t
        &root_node,                                       // Where to store the parsed plist
        &format);                                         // Where to store the format
    
    // Check if parsing succeeded
    if (err != PLIST_ERR_SUCCESS) {
        std::cerr << "Failed to parse plist data" << std::endl;
        return nullptr;
    }
    
    return root_node;  // Caller is responsible for calling plist_free()
}

// Overload for when you only want to specify inStrPlist and decompress
inline plist_t readPlist(const std::string& inStrPlist, bool decompress) {
    return readPlist(inStrPlist, "", decompress);
}

// Structure to hold object/model information from VoxelMax's scene.json
struct JsonModelInfo {
    std::string id;
    std::string parentId;
    std::string name;
    std::string dataFile;       // The .vmaxb file
    std::string paletteFile;    // The palette PNG
    std::string historyFile;    // The history file, not sure what this is
    
    // Transform information
    std::vector<double> position;      // t_p
    std::vector<double> rotation;      // t_r
    std::vector<double> scale;         // t_s
    
    // Extent information
    std::vector<double> extentCenter;  // e_c
    std::vector<double> extentMin;     // e_mi
    std::vector<double> extentMax;     // e_ma
};

// Structure to hold group information from VoxelMax's scene.json
struct JsonGroupInfo {
    std::string id;
    std::string name;
    std::vector<double> position;
    std::vector<double> rotation;
    std::vector<double> scale;
    std::vector<double> extentCenter;
    std::vector<double> extentMin;
    std::vector<double> extentMax;
    bool selected = false;
    std::string parentId;
};

// Class to parse VoxelMax's scene.json
class JsonVmaxSceneParser {
private:
    std::map<std::string, JsonModelInfo> models; // a vmax model can also be called a content
    std::map<std::string, JsonGroupInfo> groups;
    
public:
    bool parseScene(const std::string& jsonFilePath) {
        try {
            // Read the JSON file
            std::ifstream file(jsonFilePath);
            if (!file.is_open()) {
                std::cerr << "Failed to open file: " << jsonFilePath << std::endl;
                return false;
            }
            
            // Parse the JSON
            json sceneData;
            file >> sceneData;
            file.close();
            
            // Parse groups
            if (sceneData.contains("groups") && sceneData["groups"].is_array()) {
                for (const auto& group : sceneData["groups"]) {
                    JsonGroupInfo groupInfo;
                    
                    // Extract group information
                    if (group.contains("id")) groupInfo.id = group["id"];
                    if (group.contains("name")) groupInfo.name = group["name"];
                    
                    // Extract transform data
                    if (group.contains("t_p") && group["t_p"].is_array()) 
                        groupInfo.position = group["t_p"].get<std::vector<double>>();
                    
                    if (group.contains("t_r") && group["t_r"].is_array()) 
                        groupInfo.rotation = group["t_r"].get<std::vector<double>>();
                    
                    if (group.contains("t_s") && group["t_s"].is_array()) 
                        groupInfo.scale = group["t_s"].get<std::vector<double>>();
                    
                    // Extract extent data
                    if (group.contains("e_c") && group["e_c"].is_array()) 
                        groupInfo.extentCenter = group["e_c"].get<std::vector<double>>();
                    
                    if (group.contains("e_mi") && group["e_mi"].is_array()) 
                        groupInfo.extentMin = group["e_mi"].get<std::vector<double>>();
                    
                    if (group.contains("e_ma") && group["e_ma"].is_array()) 
                        groupInfo.extentMax = group["e_ma"].get<std::vector<double>>();
                    
                    // Check if selected
                    if (group.contains("s")) groupInfo.selected = group["s"].get<bool>();
                    
                    if (group.contains("pid")) groupInfo.parentId = group["pid"];
                    // Store the group
                    groups[groupInfo.id] = groupInfo;
                }
            }
            
            // Parse objects (models) , objects are instances of models
            // Maybe rename this objects, will leave for now
            if (sceneData.contains("objects") && sceneData["objects"].is_array()) {
                for (const auto& obj : sceneData["objects"]) {
                    JsonModelInfo modelInfo;
                    
                    // Extract model information
                    if (obj.contains("id")) modelInfo.id = obj["id"];
                    if (obj.contains("pid")) modelInfo.parentId = obj["pid"];
                    if (obj.contains("n")) modelInfo.name = obj["n"];
                    
                    // Extract file paths
                    if (obj.contains("data")) modelInfo.dataFile = obj["data"];// This is the canonical model
                    if (obj.contains("pal")) modelInfo.paletteFile = obj["pal"];
                    if (obj.contains("hist")) modelInfo.historyFile = obj["hist"];
                    
                    // Extract transform data
                    if (obj.contains("t_p") && obj["t_p"].is_array()) 
                        modelInfo.position = obj["t_p"].get<std::vector<double>>();
                    
                    if (obj.contains("t_r") && obj["t_r"].is_array()) 
                        modelInfo.rotation = obj["t_r"].get<std::vector<double>>();
                    
                    if (obj.contains("t_s") && obj["t_s"].is_array()) 
                        modelInfo.scale = obj["t_s"].get<std::vector<double>>();
                    
                    // Extract extent data
                    if (obj.contains("e_c") && obj["e_c"].is_array()) 
                        modelInfo.extentCenter = obj["e_c"].get<std::vector<double>>();
                    
                    if (obj.contains("e_mi") && obj["e_mi"].is_array()) 
                        modelInfo.extentMin = obj["e_mi"].get<std::vector<double>>();
                    
                    if (obj.contains("e_ma") && obj["e_ma"].is_array()) 
                        modelInfo.extentMax = obj["e_ma"].get<std::vector<double>>();
                    
                    // Store the model
                    models[modelInfo.id] = modelInfo;
                }
            }
            
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Error parsing JSON: " << e.what() << std::endl;
            return false;
        }
    }
    
    // Get the parsed models
    const std::map<std::string, JsonModelInfo>& getModels() const {
        return models;
    }
    
    // Get the parsed groups
    const std::map<std::string, JsonGroupInfo>& getGroups() const {
        return groups;
    }
    
   /* Groups models by their data file names ie contentsN.vmaxb
    * Since we can instance models, we can grab the first one when translating it to Bella
    * @return Map where:
    *         - Key: contentN.vmaxb (string)
    *         - Value: vector of models using that data file
    * Example:
    *   Input models: {"id1": model1, "id2": model2} where both use "data.file"
    *   Output: {"data.file": [model1, model2]}
    */
    std::map<std::string, std::vector<JsonModelInfo>> getModelContentVMaxbMap() const {
        std::map<std::string, std::vector<JsonModelInfo>> fileMap;
        
        for (const auto& [id, model] : models) {
            fileMap[model.dataFile].push_back(model);
        }
        
        return fileMap;
    }
    
    // Print summary of parsed data
    void printSummary() const {
        std::cout << "=========== Scene Summary ===========" << std::endl;
        std::cout << "Groups: " << groups.size() << std::endl;
        std::cout << "Models: " << models.size() << std::endl;
        
        // Print unique model files
        std::map<std::string, int> modelFiles;
        for (const auto& [id, model] : models) {
            modelFiles[model.dataFile]++;
        }
        
        std::cout << "\nModel Files:" << std::endl;
        for (const auto& [file, count] : modelFiles) {
            std::cout << "  " << file << " (used " << count << " times)" << std::endl;
        }
         
        std::cout << "\nGroups:" << std::endl;
        for (const auto& [id, group] : groups) {
            std::cout << "  " << group.name << " (ID: " << id << ")" << std::endl;
            if (!group.position.empty()) {
                std::cout << "    Position: [" 
                          << group.position[0] << ", " 
                          << group.position[1] << ", " 
                          << group.position[2] << "]" << std::endl;
            }
        }
        
        std::cout << "\nModels:" << std::endl;
        for (const auto& [id, model] : models) {
            std::cout << "  " << model.name << " (ID: " << id << ")" << std::endl;
            std::cout << "    Data: " << model.dataFile << std::endl;
            std::cout << "    Palette: " << model.paletteFile << std::endl;
            std::cout << "    Parent: " << model.parentId << std::endl;
            
            if (!model.position.empty()) {
                std::cout << "    Position: [" 
                          << model.position[0] << ", " 
                          << model.position[1] << ", " 
                          << model.position[2] << "]" << std::endl;
            }
        }
    }
};
/*
MIT License

Copyright (c) 2025 Harvey Fong

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/