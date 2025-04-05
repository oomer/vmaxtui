#pragma once
#include <string>       // For std::string
#include <iostream>

#include <efsw/FileSystem.hpp> // For file watching
#include <efsw/System.hpp> // For file watching
#include <efsw/efsw.hpp> // For file watching

//UpdateListener* global_ul = nullptr;          // Global pointer to UpdateListener
bool STOP = false;

class FileQueue; // Forward declaration of FileQueue class

bool endsWith(const std::string& str, const std::string& suffix); 


efsw::WatchID handleWatchID( efsw::WatchID watchid ) {
	switch ( watchid ) {
		case efsw::Errors::FileNotFound:
		case efsw::Errors::FileRepeated:
		case efsw::Errors::FileOutOfScope:
		case efsw::Errors::FileRemote:
		case efsw::Errors::WatcherFailed:
		case efsw::Errors::Unspecified: {
			std::cout << efsw::Errors::Log::getLastErrorLog().c_str() << std::endl;
			break;
		}
		default: {
			std::cout << "Added WatchID: " << watchid << std::endl;
		}
	}
	return watchid;
}

/// A class that manages a queue of files added/modified or deleted fromthe filesystem 
/// with both FIFO order and fast lookups
class FileQueue {
    public:
        // Default constructor
        FileQueue() = default;
    
        // Move constructor
        FileQueue(FileQueue&& other) noexcept {
            std::lock_guard<std::mutex> lock(other.mutex);
            pathVector = std::move(other.pathVector);
            pathMap = std::move(other.pathMap);
        }
    
        // Move assignment operator
        FileQueue& operator=(FileQueue&& other) noexcept {
            if (this != &other) {
                std::lock_guard<std::mutex> lock1(mutex);
                std::lock_guard<std::mutex> lock2(other.mutex);
                pathVector = std::move(other.pathVector);
                pathMap = std::move(other.pathMap);
            }
            return *this;
        }
    
        // Delete copy operations since mutexes can't be copied
        FileQueue(const FileQueue&) = delete;
        FileQueue& operator=(const FileQueue&) = delete;
    
        // Add a file to the queue if it's not already there
        bool push(const std::string& path) {
            std::lock_guard<std::mutex> lock(mutex);
            if (pathMap.find(path) == pathMap.end()) {
                pathVector.push_back(path);
                pathMap[path] = true;
                return true;
            }
            return false;
        }
    
        // Get the next file to process (FIFO order)
        bool pop(std::string& outPath) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!pathVector.empty()) {
                outPath = pathVector.front();
                pathVector.erase(pathVector.begin());
                pathMap.erase(outPath);
                return true;
            }
            return false;
        }
   
        // Get the next file to process (FIFO order)
        bool probe(std::string& outPath) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!pathVector.empty()) {
                outPath = pathVector.front();
                return true;
            }
            return false;
        }        

        // Remove a specific file by name
        bool remove(const std::string& path) {
            std::lock_guard<std::mutex> lock(mutex);
            if (pathMap.find(path) != pathMap.end()) {
                // Remove from vector using erase-remove idiom
                pathVector.erase(
                    std::remove(pathVector.begin(), pathVector.end(), path),
                    pathVector.end()
                );
                // Remove from map
                pathMap.erase(path);
                return true;
            }
            return false;
        }
    
        // Check if a file exists in the queue
        bool contains(const std::string& path) const {
            std::lock_guard<std::mutex> lock(mutex);
            return pathMap.find(path) != pathMap.end();
        }
    
        // Get the number of files in the queue
        size_t size() const {
            std::lock_guard<std::mutex> lock(mutex);
            return pathVector.size();
        }
    
        // Check if the queue is empty
        bool empty() const {
            std::lock_guard<std::mutex> lock(mutex);
            return pathVector.empty();
        }
    
        // Clear all files from the queue
        void clear() {
            std::lock_guard<std::mutex> lock(mutex);
            pathVector.clear();
            pathMap.clear();
        }
    
    private:
        std::vector<std::string> pathVector;  // Maintains FIFO order
        std::map<std::string, bool> pathMap;  // Enables fast lookups
        mutable std::mutex mutex;            // Thread safety
    };


/// Processes a file action libefsw
class UpdateListener : public efsw::FileWatchListener {
    public:
      // Modified constructor to take references to all FileQueue instances and mutexes
      UpdateListener(FileQueue& fileQueue, FileQueue& unfileQueue, FileQueue& processQueue,
                    std::mutex& fileQueueMutex, std::mutex& unfileQueueMutex, 
                    std::mutex& processQueueMutex) 
          : fileQueue_(fileQueue), 
            unfileQueue_(unfileQueue),
            processQueue_(processQueue),
            fileQueueMutex_(fileQueueMutex),
            unfileQueueMutex_(unfileQueueMutex),
            processQueueMutex_(processQueueMutex),
            should_stop_(false) {}
  
      void stop() {
          should_stop_ = true;
      }
  
      std::string getActionName( efsw::Action action ) {
          switch ( action ) {
              case efsw::Actions::Add:
                  return "Add";
              case efsw::Actions::Modified:
                  return "Modified";
              case efsw::Actions::Delete:
                  return "Delete";
              case efsw::Actions::Moved:
                  return "Moved";
              default:
                  return "Bad Action";
          }
      }
  
      void handleFileAction( efsw::WatchID watchid, 
                             const std::string& dir,
                             const std::string& filename, 
                             efsw::Action action,
                             std::string oldFilename = "" ) override {
          if (should_stop_) return;  // Early exit if we're stopping
          
          std::string actionName = getActionName( action ); 
          if (actionName == "Delete") { // always push to unfile queue, cpp will handle the rest
                std::string belPath = dir + filename;
                if (endsWith(belPath, ".bsz")) {
                    std::cout << "\n==" << "DELETE: " << dir + filename << "\n==" << std::endl;
                    {
                        std::lock_guard<std::mutex> lock(unfileQueueMutex_);
                        if (!unfileQueue_.contains(belPath)) {
                            unfileQueue_.push(belPath);
                            std::cout << "\n==" << "STOP PROCESSING: " << belPath << "\n==" << std::endl;
                        }
                    }
                }
          }
          if (actionName == "Add" || actionName == "Modified") {
              std::string belPath = dir + filename;
              std::string parentPath = dir;
              //std::cout << "parentPath: " << parentPath << std::endl;
              if (should_stop_) return;  // Check again before starting render
            
              if (endsWith(belPath, ".vmax") || endsWith(belPath, ".bsz") || endsWith(belPath, ".zip") && !endsWith(parentPath, "download/")) {
                  {
                      std::lock_guard<std::mutex> lock(fileQueueMutex_);
                      if (!fileQueue_.contains(belPath)) {
                          fileQueue_.push(belPath);
                          #ifdef _DEBUG
                                std::cout << "\n==" << "QUEUED: " << belPath << "\n==" << std::endl;
                          #endif
                      }
                  }
              }
          }
      }
    private:
      // Store references to all queues and mutexes
      FileQueue& fileQueue_;
      FileQueue& unfileQueue_;
      FileQueue& processQueue_;
      std::mutex& fileQueueMutex_;
      std::mutex& unfileQueueMutex_;
      std::mutex& processQueueMutex_;
      std::atomic<bool> should_stop_; // ctrl-c was not working, so we use this to stop the thread
  };

// Utility function to check if a string ends with a specific suffix
inline bool endsWith(const std::string& str, const std::string& suffix) {
    if (str.length() < suffix.length()) {
        return false;
    }
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}
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