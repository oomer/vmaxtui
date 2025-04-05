// vmax2bella.cpp - A program to convert VoxelMax (.vmax) files to Bella 3D scene (.bsz) files
// 
// This program reads VoxelMax files (which store voxel-based 3D models) and 
// converts them to Bella (a 3D rendering engine) scene files.

/*
# Technical Specification: VoxelMax Format

## Overview
- This document specifies a chunked voxel storage format embedded in property list (plist) files. The format provides an efficient representation of 3D voxel data through a combination of Morton-encoded spatial indexing and a sparse representation approach.

## File Structure
- Format: Property List (plist)
- Structure: Hierarchical key-value structure with nested dictionaries and arrays
- plist is compressed using the LZFSE, an open source reference c implementation is [here](https://github.com/lzfse/lzfse)

```
root
└── snapshots (array)
    └── Each snapshot (dictionary)
        ├── s (dictionary) - Snapshot data
        │   ├── id (dictionary) - Identifiers
        │   │   ├── c (int64) - Chunk ID
        │   │   ├── s (int64) - Session ID
        │   │   └── t (int64) - Type ID
        │   ├── lc (binary data) - Layer Color Usage
        │   ├── ds (binary data) - Voxel data stream
        │   ├── dlc (binary data) - Deselected Layer Color Usage
        │   └── st (dictionary) - Statistics/metadata
        │       ├── c (int64) - Count of voxels in the chunk
        │       ├── sc (int64) - Selected Count (number of selected voxels)
        │       ├── smin (array) - Selected Minimum coordinates [x,y,z,w]
        │       ├── smax (array) - Selected Maximum coordinates [x,y,z,w]
        │       ├── min (array) - Minimum coordinates of all voxels [x,y,z]
        │       ├── max (array) - Maximum coordinates of all voxels [x,y,z]
        │       └── e (dictionary) - Extent
        │           ├── o (array) - Origin/reference point [x,y,z]
        │           └── s (array) - Size/dimensions [width,height,depth]
```

## Chunking System
### Volume Organization
- The total volume is divided into chunks for efficient storage and manipulation
- Standard chunk size: 32×32×32 voxels 
- Total addressable space: 256×256×256 voxels (8×8×8 chunks)

### Morton Encoding for Chunks
- Chunk IDs are encoded using 24 bits (8 bits per dimension)
- This provides full addressability for the 8×8×8 chunks without requiring sequential traversal
- The decodeMortonChunkID function extracts x, y, z coordinates from a Morton-encoded chunk ID stored in s.id.c
- The resulting chunk coordinates are then multiplied by 32 to get the world position of the chunk

### Voxel-Level Hybrid Encoding
- Within each 32×32×32 chunk, voxels use a hybrid addressing system
- The format uses a hybrid encoding approach that combines sequential traversal and Morton encoding:
- st.min store and offset from origin of 32x32x32 chunk
- iterate through all voxels in chunk x=0 to 31, y=0 to 31, z=0 to 31 it that order
- start at origin (0,0,0) with a counter= 0
- do counter + st.min and decode this morton value to get x,y,z

### Chunk Addressing
- Chunks are only stored if they contain at least one non-empty voxel
- Each snapshot contains data for a specific chunk, identified by the 'c' value in the 's.id' dictionary

## Data Fields
### Voxel Data Stream (ds)
- Variable-length binary data
- Contains pairs of bytes for each voxel: [layer_byte, color_byte]
- Each chunk can contain up to 32,768 voxels (32×32×32)
- *Position Byte:*
    - The format uses a hybrid encoding approach that combines sequential traversal and Morton encoding:
        - Data stream can terminate at any point, avoiding the need to store all 32,768 voxel pairs

### Morton Encoding Process
- A space-filling curve that interleaves the bits of the x, y, and z coordinates
- Used to convert 3D coordinates to a 1D index and vice versa
- Creates a coherent ordering of voxels that preserves spatial locality
1. Take the binary representation of x, y, and z coordinates
2. Interleave the bits in the order: z₀, y₀, x₀, z₁, y₁, x₁, z₂, y₂, x₂, ...
3. The resulting binary number is the Morton code

- *Color Byte:*
    - Stores the color value + 1 (offset of +1 from actual color)
    - Value 0 indicates no voxel at this position
- A fully populated chunk will have 32,768 voxel pairs (65,536 bytes total in ds)

### Snapshot Accumulation
- Each snapshot contains data for a specific chunk (identified by the chunk ID)
- Multiple snapshots together build up the complete voxel model
- Later snapshots for the same chunk ID overwrite earlier ones, allowing for edits over time

### Layer Color Usage (lc)
- s.lc is a summary table (256 bytes) that tracks which colors are used anywhere in the chunk
- Each byte position (0-255) corresponds to a color palette ID
- [TODO] understand why the word layer color is used, what is a layer color

### Deselected Layer Color Usage (dlc)
- Optional 256-byte array 
- Used during editing to track which color layers the user has deselected
- Primarily for UI state preservation rather than 3D model representation

### Statistics Data (st)
- Dictionary containing metadata about the voxels in a chunk:
    - c (count): Total number of voxels in the chunk
    - sc (selectedCount): Number of currently selected voxels
    - sMin (selectedMin): Array defining minimum coordinates of current selection [x,y,z,w]
    - sMax (selectedMax): Array defining maximum coordinates of current selection [x,y,z,w]
    - min: Array defining minimum coordinates of all voxels [x,y,z]
    - max: Array defining maximum coordinates of all voxels [x,y,z]
    - e (extent): Array defining the bounding box [min_x, min_y, min_z, max_x, max_y, max_z]
    - e.o (extent.origin): Reference point or offset for extent calculations

## Coordinate Systems
### Primary Coordinate System
- Y-up coordinate system: Y is the vertical axis
- Origin (0,0,0) is at the bottom-left-front corner
- Coordinates increase toward right (X+), up (Y+), and backward (Z+)

### Addressing Scheme
1. World Space: Absolute coordinates in the full volume
2. Chunk Space: Which chunk contains a voxel (chunk_x, chunk_y, chunk_z)
3. Local Space: Coordinates within a chunk (local_x, local_y, local_z)

## Coordinate Conversion
- *World to Chunk:*
    - chunk_x = floor(world_x / 32)
    - chunk_y = floor(world_y / 32)
    - chunk_z = floor(world_z / 32)
- *World to Local:*
    - local_x = world_x % 32
    - local_y = world_y % 32
    - local_z = world_z % 32
- *Chunk+Local to World:*
    - world_x = chunk_x * 32 + local_x
    - world_y = chunk_y * 32 + local_y
    - world_z = chunk_z * 32 + local_z

## Implementation Guidance
### Reading Algorithm
1. Parse the plist file to access the snapshot array
2. For each snapshot:
   a. Extract the chunk ID from s > id > c
   b. Extract the lc and ds data
   c. Process the ds data in pairs of bytes (position, color)
   d. Calculate the world origin by decoding the Morton chunk ID and multiplying by 32
   e. Store the voxels for this chunk ID
3. Combine all snapshots to build the complete voxel model, using the chunk IDs as keys

### Writing Algorithm
1. Organize voxels by chunk (32×32×32 voxels per chunk)
2. For each non-empty chunk:
   a. Create a snapshot entry
   b. Set up the id dictionary with the appropriate chunk ID
   c. Set up a 256-byte lc array (all zeros)
   d. Create the ds data by encoding each voxel as a (position, color+1) pair
   e. Set the appropriate byte in lc to 1 if the color is used in ds
3. Add all snapshots to the array
4. Write the complete structure to a plist file

- [?] Models typically use SessionIDs to group related edits (observed values include 10 and 18)

## Snapshot Types
The 't' field in the snapshot's 's.id' dictionary indicates the type of snapshot:
  - 0: underRestore - Snapshot being restored from a previous state
  - 1: redoRestore - Snapshot being restored during a redo operation
  - 2: undo - Snapshot created for an undo operation
  - 3: redo - Snapshot created for a redo operation
  - 4: checkpoint - Snapshot created as a regular checkpoint during editing (most common)
  - 5: selection - Snapshot representing a selection operation

*/

#include <iostream> // For input/output operations (cout, cin, etc.)

// Bella SDK includes - external libraries for 3D rendering
#include "../bella_engine_sdk/src/bella_sdk/bella_engine.h" // For creating and manipulating 3D scenes in Bella
#include "../bella_engine_sdk/src/dl_core/dl_main.inl" // Core functionality from the Diffuse Logic engine

#include "oomer_voxel_vmax.h"   // common vmax voxel code and structures
#include "oomer_filequeue.h"    // common file queue code
#include "oomer_misc.h"         // common misc code


#include <fstream>
#include <thread>
#include <vector>
#include <chrono>
#include <filesystem>
#include <cstdio>  // For sprintf
#include <iostream>
#include <signal.h>
#include <string>
#include <sstream> // For string streams
#include <atomic>
#include <mutex> // Add this line for std::mutex and std::lock_guard
#include <map> // Add this line for std::map

#include <cstdlib> // For std::system
#include <stdexcept> // For std::runtime_error

#ifdef _WIN32
#include <windows.h> // For ShellExecuteW
#include <shellapi.h> // For ShellExecuteW
#include <codecvt> // For wstring_convert
#elif defined(__APPLE__) || defined(__linux__)
#include <unistd.h> // For fork, exec
#include <sys/wait.h> // For waitpid
#endif

dl::bella_sdk::Node essentialsToScene(dl::bella_sdk::Scene& belScene);
dl::bella_sdk::Node addModelToScene(dl::bella_sdk::Scene& belScene, dl::bella_sdk::Node& belWorld, const VmaxModel& vmaxModel, const std::vector<VmaxRGBA>& vmaxPalette, const std::array<VmaxMaterial, 8>& vmaxMaterial); 

dl::String programName = "vmaxtui";

std::atomic<bool> active_render(false);

dl::String currentRender;
std::mutex currentRenderMutex;  // Add mutex for thread safety

UpdateListener* global_ul = nullptr;          // Global pointer to UpdateListener
// Queues for incoming files from the efsw watcher
FileQueue fileQueue;  
FileQueue unfileQueue;  
FileQueue processQueue;  
std::mutex fileQueueMutex;  // Add mutex for thread safety
std::mutex unfileQueueMutex;  // Add mutex for thread safety
std::mutex processQueueMutex;  // Add mutex for thread safety

//Forward declares
dl::bella_sdk::Scene convertVmaxToBella( const dl::String& vmaxDirName);

// Signal handler for ctrl-c
void sigend( int ) {
	std::cout << std::endl << "Bye bye" << std::endl;
	STOP = true;
	if (global_ul) {  // Use the global pointer
		global_ul->stop();
	}
	// Give a short time for cleanup
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	exit(0);  // Force exit after cleanup
}

static int s_logCtx = 0;
static void log(void* /*ctx*/, dl::LogType type, const char* msg)
{
    switch (type)
    {
    case dl::LogType_Info:
        DL_PRINT("[INFO] %s\n", msg);
        break;
    case dl::LogType_Warning:
        DL_PRINT("[WARN] %s\n", msg);
        break;
    case dl::LogType_Error:
        DL_PRINT("[ERROR] %s\n", msg);
        break;
    case dl::LogType_Custom:
        DL_PRINT("%s\n", msg);
        break;
    }
}

/*
 * MyEngineObserver Class
 * This class receives callbacks from the Bella rendering engine to track rendering progress.
 * It implements the EngineObserver interface and provides methods to:
 * - Handle render start/stop events
 * - Track rendering progress
 * - Handle error conditions
 * - Store and retrieve the current progress state
 * - Can be passed by reference unlike Engine
 */
 struct MyEngineObserver : public dl::bella_sdk::EngineObserver
 {
 public:
     // Called when a rendering pass starts
     void onStarted(dl::String pass) override
     {
         std::cout << "Started pass " << pass.buf() << std::endl;
         dl::logInfo("Started pass %s", pass.buf());
     }
 
     // Called to update the current status of rendering
     //void onStatus(String pass, String status) override
     //{
     //    logInfo("%s [%s]", status.buf(), pass.buf());
     //}
 
     // Called to update rendering progress (percentage, time remaining, etc)
     void onProgress(dl::String pass, dl::bella_sdk::Progress progress) override
     {
         std::cout << progress.toString().buf() << std::endl;
         //setString(new std::string(progress.toString().buf()));
         //logInfo("%s [%s]", progress.toString().buf(), pass.buf());
     }
 
     //void onImage(String pass, Image image) override
     //{
     //    logInfo("We got an image %d x %d.", (int)image.width(), (int)image.height());
     //}  
 
     // Called when an error occurs during rendering
     void onError(dl::String pass, dl::String msg) override
     {
         dl::logError("%s [%s]", msg.buf(), pass.buf());
     }
 
     // Called when a rendering pass completes
     void onStopped(dl::String pass) override
     {
         dl::logInfo("Stopped %s", pass.buf());
         active_render = false;
     }
 
     // Returns the current progress as a string
     std::string getProgress() const {
         std::string* currentProgress = progressPtr.load();
         if (currentProgress) {
             return *currentProgress;
         } else {
             return "";
         }
     }
 
     // Cleanup resources in destructor
     ~MyEngineObserver() {
         setString(nullptr);
     }
 private:
     // Thread-safe pointer to current progress string
     std::atomic<std::string*> progressPtr{nullptr};
 
     // Helper function to safely update the progress string
     void setString(std::string* newStatus) {
         std::string* oldStatus = progressPtr.exchange(newStatus);
         delete oldStatus;  // Clean up old string if it exists
     }
 };

int DL_main(dl::Args& args) {
    args.add("i", "input", "", "vmax directory or vmax.zip file");
    args.add("o", "output", "", "set output bella file name");
    args.add("tp",  "thirdparty",   "",   "prints third party licenses");
    args.add("li",  "licenseinfo",   "",   "prints license info");
    args.add("w",  "watchdir",   "",   "watch directory for changes");

    // If --help was requested, print help and exit
    if (args.helpRequested()) {
        std::cout << args.help("© 2025 Harvey Fong",programName, "1.0") << std::endl;
        return 0;
    }
    
    // If --licenseinfo was requested, print license info and exit
    if (args.have("--licenseinfo"))
    {
        std::cout << programName.buf() << "\n" << initializeMyLicense() << std::endl;
        return 0;
    }
 
    // If --thirdparty was requested, print third-party licenses and exit
    if (args.have("--thirdparty"))
    {
        std::cout << initializeThirdPartyLicences() << std::endl;
        return 0;
    }
    if (args.have("--input"))
    {
        dl::String bszName;
        dl::String vmaxDirName = args.value("--input");
        auto vmaxPath = dl::Path();
        if (!vmaxPath.exists(vmaxDirName)) {
            std::cout << "Cannot find directory " << vmaxDirName.buf() << std::endl;
            return 0;
        }

        bszName = vmaxDirName.replace("vmax", "bsz");
        dl::bella_sdk::Scene belScene = convertVmaxToBella(vmaxDirName);
        belScene.write(bszName.buf());
     }

    if (args.have("--watchdir")) {
        std::cout << "VmaxTUI server started ..." << std::endl;
        std::string watchDir = args.value("--watchdir").buf();
        // Initialize the UpdateListener with references to our queues and mutexes
        global_ul = new UpdateListener( fileQueue, 
                                        unfileQueue, 
                                        processQueue, 
                                        fileQueueMutex, 
                                        unfileQueueMutex, 
                                        processQueueMutex);
    
        // TODO: Set up your file watcher and add the listener to it if needed
        efsw::FileWatcher* fileWatcher = new efsw::FileWatcher();
        fileWatcher->addWatch(watchDir, global_ul, true); // true for recursive
        fileWatcher->watch();

        // Create persistent instances outside the loop
        FileQueue renderQueue;
        FileQueue renderUnqueue;
        dl::bella_sdk::Engine engine;
        MyEngineObserver engineObserver;
        engine.subscribe(&engineObserver);
        engine.scene().loadDefs();

        while (true) {
            // Append items from incoming queues to our persistent queues for thread safety
            if (renderQueue.empty() ) {
                renderUnqueue.clear(); // catch edge case where deletes have accumulated
            }
            {
                std::lock_guard<std::mutex> lock(fileQueueMutex);
                std::string path;
                while (fileQueue.pop(path)) {
                    renderQueue.push(path);
                }
                fileQueue.clear();
            }
            
            {
                std::lock_guard<std::mutex> lock(unfileQueueMutex);
                std::string path;
                while (unfileQueue.pop(path)) {
                    renderUnqueue.push(path);
                }
                unfileQueue.clear();
            }

            // Process items from the persistent queues one at a time
            // Will we block while rendering
            // We should be allowed to delete .bsz files while rendering in loop
            // so we should have a render thread with engine.isrendering()
            // while the purpos eof the is main loop is event processing

            // Process the files without holding the mutex
            bool expected = false;
            
            // This is an atomic operation that does two things at once:
            // 1. Checks if active_render equals expected (false)
            // 2. If they are equal, sets active_render to true
            // 
            // The operation is atomic, meaning no other thread can interfere
            // between the check and the set. This prevents two threads from
            // both thinking they can start rendering at the same time.
            //
            // Returns true if the exchange was successful (we got the render slot)
            // Returns false if active_render was already true (someone else is rendering)
            dl::String currentRender;
            dl::String belPath;
            if (!renderQueue.empty()) {
                if (active_render.compare_exchange_strong(expected, true)) {
                    std::string path;
                    renderQueue.pop(path);
                    belPath = dl::String(path.c_str());
                    if (belPath.endsWith(".bsz")) {
                        engine.loadScene(belPath);
                        engine.scene().camera()["resolution"] = dl::Vec2 {200, 200};
                        engine.start();
                        currentRender = belPath;
                        std::cout << "\n==" << "RENDERING: " << path << "\n==" << std::endl;
                    } else if (belPath.endsWith(".vmax")) {
                        convertVmaxToBella(belPath);
                    }
                } else {
                    std::string path;
                    while (renderUnqueue.pop(path)) { // pop all the deletes
                        if (dl::String(path.c_str()) == currentRender) {
                            std::cout << "\n==\nStopping render" << path<< std::endl;
                            engine.stop();
                            active_render = false;
                        } else if (renderQueue.contains(path)) { // dequeue deletes
                            renderQueue.remove(path);
                        } 
                    }
                }
            }



            //std::cout << "Render Queue Size: " << renderQueue.size() << std::endl;
            //std::cout << "Render Unqueue Size: " << renderUnqueue.size() << std::endl;


            std::this_thread::sleep_for(std::chrono::milliseconds(900));
        }
    }

    return 0;
}



// @param belScene - the scene to create the essentials in
// @return - the world node
dl::bella_sdk::Node essentialsToScene(dl::bella_sdk::Scene& belScene) {
    // Create the basic scene elements in Bella
    // Each line creates a different type of node in the scene auto belBeautyPass     = belScene.createNode("beautyPass","oomerBeautyPass","oomerBeautyPass");
    auto belWorld = belScene.world();       // Get scene world root
    {
        dl::bella_sdk::Scene::EventScope es(belScene);

        auto belCamForm    = belScene.createNode("xform","oomerCameraXform","oomerCameraXform");
        auto belCam        = belScene.createNode("camera","oomerCamera","oomerCamera");
        auto belSensor         = belScene.createNode("sensor","oomerSensor","oomerSensor");
        auto belLens           = belScene.createNode("thinLens","oomerThinLens","oomerThinLens");
        auto belImageDome      = belScene.createNode("imageDome","oomerImageDome","oomerImageDome");
        auto belGroundPlane    = belScene.createNode("groundPlane","oomerGroundPlane","oomerGroundPlane");

        auto belBeautyPass     = belScene.createNode("beautyPass","oomerBeautyPass","oomerBeautyPass");
        auto belGroundMat      = belScene.createNode("quickMaterial","oomerGroundMat","oomerGroundMat");
        auto belSun            = belScene.createNode("sun","oomerSun","oomerSun");
        auto belColorDome   = belScene.createNode("colorDome","oomerColorDome","oomerColorDome");
        auto belSettings = belScene.settings(); // Get scene settings
        // Configure camera
        belCam["resolution"]    = dl::Vec2 {1920, 1080};  // Set resolution to 1080p
        belCam["lens"]          = belLens;               // Connect camera to lens
        belCam["sensor"]        = belSensor;             // Connect camera to sensor
        belCamForm.parentTo(belWorld);                  // Parent camera transform to world
        belCam.parentTo(belCamForm);                   // Parent camera to camera transform

        // Position the camera with a transformation matrix
        belCamForm["steps"][0]["xform"] = dl::Mat4 {0.525768608156, -0.850627633385, 0, 0, -0.234464751651, -0.144921468924, -0.961261695938, 0, 0.817675761479, 0.505401223947, -0.275637355817, 0, -88.12259018466, -54.468125200218, 50.706001690932, 1};

        // Configure environment (image-based lighting)
        belImageDome["ext"]            = ".jpg";
        belImageDome["dir"]            = "./res";
        belImageDome["multiplier"]     = 6.0f;
        belImageDome["file"]           = "DayEnvironmentHDRI019_1K-TONEMAPPED";
        belImageDome["overrides"]["background"]     = belColorDome;
        belColorDome["zenith"] = dl::Rgba{1.0f, 1.0f, 1.0f, 1.0f};
        belColorDome["horizon"] = dl::Rgba{.85f, 0.76f, 0.294f, 1.0f};
        belColorDome["altitude"] = 14.0f;
        // Configure ground plane
        belGroundPlane["elevation"]    = -.5f;
        belGroundPlane["material"]     = belGroundMat;

        /* Commented out: Sun configuration
        belSun["size"]    = 20.0f;
        belSun["month"]    = "july";
        belSun["rotation"]    = 50.0f;*/

        // Configure materials
        belGroundMat["type"] = "metal";
        belGroundMat["roughness"] = 22.0f;
        belGroundMat["color"] = dl::Rgba{0.138431623578, 0.5, 0.3, 1.0};

        // Set up scene settings
        belSettings["beautyPass"]  = belBeautyPass;
        belSettings["camera"]      = belCam;
        belSettings["environment"] = belColorDome;
        belSettings["iprScale"]    = 100.0f;
        belSettings["threads"]     = dl::bella_sdk::Input(0);  // Auto-detect thread count
        belSettings["groundPlane"] = belGroundPlane;
        belSettings["iprNavigation"] = "maya";  // Use Maya-like navigation in viewer
        //settings["sun"] = sun;

        auto belVoxel          = belScene.createNode("box","oomerVoxel","oomerVoxel");
        auto belLiqVoxel       = belScene.createNode("box","oomerLiqVoxel","oomerLiqVoxel");
        auto belVoxelForm      = belScene.createNode("xform","oomerVoxelXform","oomerVoxelXform");
        auto belLiqVoxelForm   = belScene.createNode("xform","oomerLiqVoxelXform","oomerLiqVoxelXform");
        auto belVoxelMat       = belScene.createNode("orenNayar","oomerVoxelMat","oomerVoxelMat");
        auto belMeshVoxel   = belScene.createNode("mesh", "oomerMeshVoxel");
        #include "resources/smoothcube.h"
       // Configure voxel box dimensions
        belVoxel["radius"]  = 0.33f;
        belVoxel["sizeX"]   = 0.99f;
        belVoxel["sizeY"]   = 0.99f;
        belVoxel["sizeZ"]   = 0.99f;

        // Less gap to make liquid look better, allows more light to pass through
        belLiqVoxel["sizeX"]    = 0.99945f;
        belLiqVoxel["sizeY"]    = 0.99945f;
        belLiqVoxel["sizeZ"]    = 0.99945f;

        belVoxel.parentTo(belVoxelForm);
        belVoxelForm["steps"][0]["xform"] = dl::Mat4 {0.999,0,0,0,0,0.999,0,0,0,0,0.999,0,0,0,0,1};
        belVoxelMat["reflectance"] = dl::Rgba{0.0, 0.0, 0.0, 1.0};
        belVoxelForm["material"] = belVoxelMat;


    }
    return belWorld;
}


// Only add the canonical model to the scene
// We'll use xforms to instance the model
// Each model is stores in contentsN.vmaxb as a lzfe compressed plist
// Each model has a paletteN.png that maps 0-255 to colors
// The model stored in contentsN.vmaxb can have mulitple snapshots
// Each snapshot contains a chunkID, and a datastream
// The datastream contains the voxels for the snapshot
// The voxels are stored in chunks, each chunk is 8x8x8 voxels
// The chunks are stored in a morton order
dl::bella_sdk::Node addModelToScene(dl::bella_sdk::Scene& belScene, dl::bella_sdk::Node& belWorld, const VmaxModel& vmaxModel, const std::vector<VmaxRGBA>& vmaxPalette, const std::array<VmaxMaterial, 8>& vmaxMaterial) {
    // Create Bella scene nodes for each voxel
    int i = 0;
    dl::String modelName = dl::String(vmaxModel.vmaxbFileName.c_str());
    dl::String canonicalName = modelName.replace(".vmaxb", "");
    dl::bella_sdk::Node belCanonicalNode;
    {
        dl::bella_sdk::Scene::EventScope es(belScene);

        auto belVoxel = belScene.findNode("oomerVoxel");
        auto belLiqVoxel = belScene.findNode("oomerLiqVoxel");
        auto belMeshVoxel = belScene.findNode("oomerMeshVoxel");
        auto belVoxelForm = belScene.findNode("oomerVoxelXform");
        auto belLiqVoxelForm = belScene.findNode("oomerLiqVoxelXform");

        auto modelXform = belScene.createNode("xform", canonicalName, canonicalName);
        modelXform["steps"][0]["xform"] = dl::Mat4 {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        for (const auto& [material, colorID] : vmaxModel.getUsedMaterialsAndColors()) {
            for (int color : colorID) {
                auto belInstancer  = belScene.createNode("instancer",
                                canonicalName + dl::String("Material") + dl::String(material) + dl::String("Color") + dl::String(color));
                auto xformsArray = dl::ds::Vector<dl::Mat4f>();
                belInstancer["steps"][0]["xform"] = dl::Mat4 {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
                belInstancer.parentTo(modelXform);

                auto belMaterial  = belScene.createNode("quickMaterial",
                                canonicalName + dl::String("vmaxMat") + dl::String(material) + dl::String("Color") + dl::String(color));


                if(material==7) {
                    belMaterial["type"] = "liquid";
                    //belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
                    belMaterial["liquidDepth"] = 100.0f;
                    belMaterial["ior"] = 1.11f;
                } else if(material==6 || vmaxPalette[color-1].a < 255) {
                    belMaterial["type"] = "glass";
                    belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
                    belMaterial["glassDepth"] = 200.0f;
                } else if(vmaxMaterial[material].metalness > 0.1f) {
                    belMaterial["type"] = "metal";
                    belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
                } else if(vmaxMaterial[material].transmission > 0.0f) {
                    belMaterial["type"] = "dielectric";
                    belMaterial["transmission"] = vmaxMaterial[material].transmission;
                } else if(vmaxMaterial[material].emission > 0.0f) {
                    belMaterial["type"] = "emitter";
                    belMaterial["emitterUnit"] = "radiance";
                    std::cout << "vmaxMaterial[material].emission: " << vmaxMaterial[material].emission << std::endl;
                    std::cout << "belMaterial[" << belMaterial["type"].asString() << "]" << std::endl;
                    belMaterial["emitterEnergy"] = vmaxMaterial[material].emission*500.0f;
                } else {
                    belMaterial["type"] = "plastic";
                    belMaterial["roughness"] = vmaxMaterial[material].roughness * 100.0f;
                }
                belInstancer["material"] = belMaterial;
                // Convert 0-255 to 0-1 , remember to -1 color index becuase voxelmax needs 0 to indicate no voxel
                double bellaR = static_cast<double>(vmaxPalette[color-1].r)/255.0;
                double bellaG = static_cast<double>(vmaxPalette[color-1].g)/255.0;
                double bellaB = static_cast<double>(vmaxPalette[color-1].b)/255.0;
                double bellaA = static_cast<double>(vmaxPalette[color-1].a)/255.0;
                belMaterial["color"] = dl::Rgba{ // convert sRGB to linear
                    srgbToLinear(bellaR), 
                    srgbToLinear(bellaG), 
                    srgbToLinear(bellaB), 
                    bellaA // alpha is already linear
                }; // colors ready to use in Bella

                // Get all voxels for this material/color combination
                const std::vector<VmaxVoxel>& voxelsOfType = vmaxModel.getVoxels(material, color);
                int showchunk =0;

                // Right now we group voxels by MatCol ie Mat0Col2
                // But voxels are stored in chunks with many colors 
                // Since we aren't grouping voxels in chunks, we need to traverse the voxels 
                // and offset each voxel by the morton decode of chunk index
                for (const auto& eachvoxel : voxelsOfType) {
                    // Get chunk coordinates and world origin
                    uint32_t _tempx, _tempy, _tempz;
                    decodeMorton3DOptimized(eachvoxel.chunkID, _tempx, _tempy, _tempz); // index IS the morton code
                    int worldOffsetX = _tempx * 24; // get world loc within 256x256x256 grid
                    int worldOffsetY = _tempy * 24; // Don't know why we need to multiply by 24
                    int worldOffsetZ = _tempz * 24; // use to be 32
                    xformsArray.push_back( dl::Mat4f{  1, 0, 0, 0, 
                                                        0, 1, 0, 0, 
                                                        0, 0, 1, 0, 
                                                        static_cast<float>(eachvoxel.x + worldOffsetX),
                                                        static_cast<float>(eachvoxel.y + worldOffsetY),
                                                        static_cast<float>(eachvoxel.z + worldOffsetZ), 1 });
                }
                belInstancer["steps"][0]["instances"] = xformsArray;
                if(material==7) {
                    belLiqVoxel.parentTo(belInstancer);
                } else {
                    belMeshVoxel.parentTo(belInstancer);
                }
                if(vmaxMaterial[material].emission > 0.0f) {
                    belVoxelForm.parentTo(belInstancer);
                }
            }
        }
        return modelXform;
    }
    return dl::bella_sdk::Node();
}


dl::bella_sdk::Scene convertVmaxToBella( const dl::String& vmaxDirName)
{
    //dl::String bszName;
    //bszName = vmaxDirName.replace(".vmax", ".bsz");

    // Create a new scene
    dl::bella_sdk::Scene belScene;
    belScene.loadDefs();
    auto belWorld = belScene.world(true);

    // scene.json is the toplevel file that hierarchically defines the scene
    // it contains nestable groups (containers) and objects (instances) that point to resources that define the object
    // objects properties
    //  - transformation matrix
    // objects resources
    /// - reference a contentsN.vmaxb (lzfse compressed plist file) that contains a 256x256x256 voxel "model"
    //  - reference to a paletteN.png that defines the 256 24bit colors used in the 256x256x256 model
    //  - reference to a paletteN.settings.vmaxpsb (plist file) that defines the 8 materials used in the "model"
    // In scenegraph parlance a group is a xform, a object is a transform with a child geometry 
    // multiple objects can point to the same model creating what is known as an instance
    JsonVmaxSceneParser vmaxSceneParser;
    vmaxSceneParser.parseScene((vmaxDirName+"/scene.json").buf());

    #ifdef _DEBUG
        vmaxSceneParser.printSummary();
    #endif
    std::map<std::string, JsonGroupInfo> jsonGroups = vmaxSceneParser.getGroups();
    std::map<dl::String, dl::bella_sdk::Node> belGroupNodes; // Map of UUID to bella node
    std::map<dl::String, dl::bella_sdk::Node> belCanonicalNodes; // Map of UUID to bella node

    // First pass to create all the Bella nodes for the groups
    for (const auto& [groupName, groupInfo] : jsonGroups) { 
        dl::String belGroupUUID = dl::String(groupName.c_str());
        belGroupUUID = belGroupUUID.replace("-", "_"); // Make sure the group name is valid for a Bella node name
        belGroupUUID = "_" + belGroupUUID; // Make sure the group name is valid for a Bella node name
        belGroupNodes[belGroupUUID] = belScene.createNode("xform", belGroupUUID, belGroupUUID); // Create a Bella node for the group


        VmaxMatrix4x4 objectMat4 = combineVmaxTransforms(groupInfo.rotation[0], 
                                          groupInfo.rotation[1], 
                                          groupInfo.rotation[2], 
                                          groupInfo.rotation[3],
                                          groupInfo.position[0], 
                                          groupInfo.position[1], 
                                          groupInfo.position[2], 
                                          groupInfo.scale[0], 
                                          groupInfo.scale[1], 
                                          groupInfo.scale[2]);

        belGroupNodes[belGroupUUID]["steps"][0]["xform"] = dl::Mat4({
            objectMat4.m[0][0], objectMat4.m[0][1], objectMat4.m[0][2], objectMat4.m[0][3],
            objectMat4.m[1][0], objectMat4.m[1][1], objectMat4.m[1][2], objectMat4.m[1][3],
            objectMat4.m[2][0], objectMat4.m[2][1], objectMat4.m[2][2], objectMat4.m[2][3],
            objectMat4.m[3][0], objectMat4.m[3][1], objectMat4.m[3][2], objectMat4.m[3][3]
            });
    }

    // json file is allowed the parent to be defined after the child, requiring us to create all the bella nodes before we can parent them
    for (const auto& [groupName, groupInfo] : jsonGroups) { 
        dl::String belGroupUUID = dl::String(groupName.c_str());
        belGroupUUID = belGroupUUID.replace("-", "_");
        belGroupUUID = "_" + belGroupUUID;
        if (groupInfo.parentId == "") {
            belGroupNodes[belGroupUUID].parentTo(belWorld); // Group without a parent is a child of the world
        } else {
            dl::String belPPPGroupUUID = dl::String(groupInfo.parentId.c_str());
            belPPPGroupUUID = belPPPGroupUUID.replace("-", "_");
            belPPPGroupUUID = "_" + belPPPGroupUUID;
            dl::bella_sdk::Node myParentGroup = belGroupNodes[belPPPGroupUUID]; // Get bella obj
            belGroupNodes[belGroupUUID].parentTo(myParentGroup); // Group underneath a group
        }
    }

    // Efficiently process unique models by examining only the first instance of each model type.
    // Example: If we have 100 instances of 3 different models:
    //   "model1.vmaxb": [instance1, instance2, ..., instance50],
    //   "model2.vmaxb": [instance1, ..., instance30],
    //   "model3.vmaxb": [instance1, ..., instance20]
    // This loop runs only 3 times (once per unique model), not 100 times (once per instance)
    
    auto modelVmaxbMap = vmaxSceneParser.getModelContentVMaxbMap(); 
    std::vector<VmaxModel> allModels;
    std::vector<std::vector<VmaxRGBA>> vmaxPalettes; // one palette per model
    std::vector<std::array<VmaxMaterial, 8>> vmaxMaterials; // one material per model
    //std::vector<std::array<VmaxMaterial, 8>> allMaterials; // one material per model
    //std::vector<std::vector<VmaxRGBA>> allPalettes;

    essentialsToScene(belScene); // create the basic scene elements in Bella
    
    // Loop over each model defined in scene.json and process the first instance 
    // This will be out canonical models, not instances
    // todo rename model to objects as per vmax
    for (const auto& [vmaxContentName, vmaxModelList] : modelVmaxbMap) { 
        VmaxModel currentVmaxModel(vmaxContentName);
        const auto& jsonModelInfo = vmaxModelList.front(); // get the first model, others are instances at the scene level
        std::vector<double> position = jsonModelInfo.position;
        std::vector<double> rotation = jsonModelInfo.rotation;
        std::vector<double> scale = jsonModelInfo.scale;
        std::vector<double> extentCenter = jsonModelInfo.extentCenter;

        // Get this models colors from the paletteN.png 
        dl::String pngName = vmaxDirName + "/" + jsonModelInfo.paletteFile.c_str();
        auto materialName = pngName.replace(".png", ".settings.vmaxpsb");
        vmaxPalettes.push_back(read256x1PaletteFromPNG(pngName.buf())); // gather all models palettes
        if (vmaxPalettes.empty()) { throw std::runtime_error("Failed to read palette from: png " ); }

        // Read contentsN.vmaxb plist file, lzfse compressed
        dl::String modelFileName = vmaxDirName + "/" + jsonModelInfo.dataFile.c_str();
        plist_t plist_model_root = readPlist(modelFileName.buf(), true); // decompress=true

        // There will one or more snapshots in the plist file
        // Each snapshot is a capture of a 32x32x32 voxel chunk at a point in time
        // A chunkId is a morton code that uniquely identifies the chunk is a 8x8x8 array within 256x256x256 model volume
        // The highest index snapshot is the current state of the model
        // One can traverse the snapshots in reverse to get the history of the model frok inception
        plist_t plist_snapshots_array = plist_dict_get_item(plist_model_root, "snapshots");
        uint32_t snapshots_array_size = plist_array_get_size(plist_snapshots_array);
        #ifdef _DEBUG
            std::cout << "vmaxContentName: " << vmaxContentName << std::endl;
            std::cout << "snapshots_array_size: " << snapshots_array_size << std::endl;
        #endif

        // Create a VmaxModel object
        for (uint32_t i = 0; i < snapshots_array_size; i++) {
            plist_t plist_snapshot = plist_array_get_item(plist_snapshots_array, i);
            plist_t plist_chunk = getNestedPlistNode(plist_snapshot, {"s", "id", "c"});
            plist_t plist_datastream = getNestedPlistNode(plist_snapshot, {"s", "ds"});
            uint64_t chunkID;
            plist_get_uint_val(plist_chunk, &chunkID);
            VmaxChunkInfo chunkInfo = vmaxChunkInfo(plist_snapshot);
            #ifdef _DEBUG
                std::cout << "\nChunkID: " << chunkInfo.id << std::endl;
                std::cout << "TypeID: " << chunkInfo.type << std::endl;
                std::cout << "MortonCode: " << chunkInfo.mortoncode << "\n" <<std::endl;
            #endif


            std::vector<VmaxVoxel> xvoxels = vmaxVoxelInfo(plist_datastream, chunkInfo.id, chunkInfo.mortoncode);

            for (const auto& voxel : xvoxels) {
                currentVmaxModel.addVoxel(voxel.x, voxel.y, voxel.z, voxel.material, voxel.palette ,chunkInfo.id, chunkInfo.mortoncode);
            }
        }
        allModels.push_back(currentVmaxModel);

        // Parse the materials store in paletteN.settings.vmaxpsb    
        plist_t plist_material = readPlist(materialName.buf(),false); // decompress=false
        std::array<VmaxMaterial, 8> currentMaterials = getVmaxMaterials(plist_material);
        vmaxMaterials.push_back(currentMaterials);
    }
    // Need to access voxels by material and color groupings
    // Models are canonical models, not instances
    // Vmax objects are instances of models
    // First create canonical models and they are NOT attached to belWorld
    int modelIndex=0;
    for (const auto& eachModel : allModels) {
        dl::bella_sdk::Node belModel = addModelToScene(belScene, belWorld, eachModel, vmaxPalettes[modelIndex], vmaxMaterials[modelIndex]);
        dl::String lllmodelName = dl::String(eachModel.vmaxbFileName.c_str());
        dl::String lllcanonicalName = lllmodelName.replace(".vmaxb", "");
        belCanonicalNodes[lllcanonicalName.buf()] = belModel;
        std::cout << lllcanonicalName.buf() << std::endl;
        modelIndex++;
    }

    // Second Loop through each vmax object and create an instance of the canonical model
    // This is the instances of the models, we did a pass to create the canonical models earlier
    for (const auto& [vmaxContentName, vmaxModelList] : modelVmaxbMap) { 
        VmaxModel currentVmaxModel(vmaxContentName);
        for(const auto& jsonModelInfo : vmaxModelList) {
            std::vector<double> position = jsonModelInfo.position;
            std::vector<double> rotation = jsonModelInfo.rotation;
            std::vector<double> scale = jsonModelInfo.scale;
            std::vector<double> extentCenter = jsonModelInfo.extentCenter;
            auto jsonParentId = jsonModelInfo.parentId;
            auto belParentId = dl::String(jsonParentId.c_str());
            dl::String belParentGroupUUID = belParentId.replace("-", "_");
            belParentGroupUUID = "_" + belParentGroupUUID;

            auto belObjectId = dl::String(jsonModelInfo.id.c_str());
            belObjectId = belObjectId.replace("-", "_");
            belObjectId = "_" + belObjectId;

            dl::String getCanonicalName = dl::String(jsonModelInfo.dataFile.c_str());
            dl::String canonicalName = getCanonicalName.replace(".vmaxb", "");
            //get bel node from canonical name
            auto belCanonicalNode = belCanonicalNodes[canonicalName.buf()];
            auto foofoo = belScene.findNode(canonicalName);


            VmaxMatrix4x4 objectMat4 = combineVmaxTransforms(rotation[0], 
                                                             rotation[1], 
                                                             rotation[2], 
                                                             rotation[3],
                                                             position[0], 
                                                             position[1], 
                                                             position[2], 
                                                             scale[0], 
                                                             scale[1], 
                                                             scale[2]);

            auto belNodeObjectInstance = belScene.createNode("xform", belObjectId, belObjectId);
            belNodeObjectInstance["steps"][0]["xform"] = dl::Mat4({
                objectMat4.m[0][0], objectMat4.m[0][1], objectMat4.m[0][2], objectMat4.m[0][3],
                objectMat4.m[1][0], objectMat4.m[1][1], objectMat4.m[1][2], objectMat4.m[1][3],
                objectMat4.m[2][0], objectMat4.m[2][1], objectMat4.m[2][2], objectMat4.m[2][3],
                objectMat4.m[3][0], objectMat4.m[3][1], objectMat4.m[3][2], objectMat4.m[3][3]
                });

            if (jsonParentId == "") {
                belNodeObjectInstance.parentTo(belScene.world());
            } else {
                belNodeObjectInstance.parentTo(belGroupNodes[belParentGroupUUID]);
            }
            foofoo.parentTo(belNodeObjectInstance);
        }
    }
    return belScene;
}