#!/bin/bash

# Set up directories
echo "Setting up directories..."
mkdir -p build-web
mkdir -p web
mkdir -p assets/models

# Check for Emscripten SDK
if [ -z "$EMSDK" ]; then
  echo "Error: EMSDK environment variable not set!"
  echo "Please install and activate the Emscripten SDK first."
  exit 1
fi

echo "Using Emscripten SDK at: $EMSDK"

# Determine input model
DEFAULT_OBJ="assets/models/snakeH.obj"
DEFAULT_GLTF="assets/models/snakeH.gltf"

# Create GLTF from OBJ if not present
if [ -f "$DEFAULT_OBJ" ] && [ ! -f "$DEFAULT_GLTF" ]; then
  echo "Converting $DEFAULT_OBJ to $DEFAULT_GLTF"
  ./obj2gltf "$DEFAULT_OBJ" "$DEFAULT_GLTF"
fi

# Set default model to use
if [ -f "$DEFAULT_GLTF" ]; then
  DEFAULT_MODEL="snakeH.gltf"
elif [ -f "assets/models/cube.gltf" ]; then
  DEFAULT_MODEL="cube.gltf"
else
  echo "No suitable GLTF model found. Exiting."
  exit 1
fi

MODEL_BASENAME="${DEFAULT_MODEL%.*}"
echo "Using $DEFAULT_MODEL as default model for WebGL build."

# Get Assimp paths from environment (passed from Makefile)
if [ -n "$ASSIMP_INCLUDE_DIR" ]; then
  echo "Using Assimp include directory: $ASSIMP_INCLUDE_DIR"
else
  # Try to detect Assimp if not provided
  echo "Assimp include directory not provided, attempting auto-detection..."

  # Common Assimp locations to check
  ASSIMP_PATHS=(
    "/opt/homebrew/opt/assimp/include"
    "/opt/homebrew/include"
    "/usr/local/include"
    "/usr/include"
  )

  for path in "${ASSIMP_PATHS[@]}"; do
    if [ -f "$path/assimp/scene.h" ]; then
      ASSIMP_INCLUDE_DIR="$path"
      echo "Found Assimp at: $ASSIMP_INCLUDE_DIR"
      break
    fi
  done

  if [ -z "$ASSIMP_INCLUDE_DIR" ]; then
    echo "Warning: Assimp include directory not found."
    echo "WebGL build will use custom OBJ loader instead of Assimp."
  fi
fi

# Find GLM from system paths
GLM_FOUND=0
GLM_PATHS=(
  "/opt/homebrew/Cellar/glm/1.0.1/include"
  "/opt/homebrew/include"
  "/usr/local/include"
  "/usr/include"
  "./deps/glm"
)

for path in "${GLM_PATHS[@]}"; do
  if [ -f "$path/glm/glm.hpp" ]; then
    GLM_INCLUDE_DIR="$path"
    GLM_FOUND=1
    echo "Found GLM at: $GLM_INCLUDE_DIR"
    break
  fi
done

# If GLM is not found, download it
if [ $GLM_FOUND -eq 0 ]; then
  echo "GLM not found in system paths. Downloading GLM..."
  mkdir -p deps
  if command -v git &>/dev/null; then
    git clone --depth 1 https://github.com/g-truc/glm.git deps/glm
    GLM_INCLUDE_DIR="./deps/glm"
    echo "Downloaded GLM to: $GLM_INCLUDE_DIR"
  else
    echo "Git not found. Please install git or download GLM manually."
    exit 1
  fi
fi

# Download and setup tiny_gltf if not already present
if [ ! -d "deps/tiny_gltf" ]; then
  echo "Setting up tiny_gltf..."
  mkdir -p deps/tiny_gltf

  if command -v curl &>/dev/null; then
    curl -s -o deps/tiny_gltf/tiny_gltf.h https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h
    curl -s -o deps/tiny_gltf/json.hpp https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp
    curl -s -o deps/tiny_gltf/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
    curl -s -o deps/tiny_gltf/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
    echo "Downloaded tiny_gltf and dependencies"
  elif command -v wget &>/dev/null; then
    wget -q -O deps/tiny_gltf/tiny_gltf.h https://raw.githubusercontent.com/syoyo/tinygltf/master/tiny_gltf.h
    wget -q -O deps/tiny_gltf/json.hpp https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp
    wget -q -O deps/tiny_gltf/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
    wget -q -O deps/tiny_gltf/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
    echo "Downloaded tiny_gltf and dependencies"
  else
    echo "Neither curl nor wget found. Please install one of them or download tiny_gltf manually."
    exit 1
  fi
fi

# Check for model files
echo "Checking model files in assets/models directory..."
ls -la assets/models

# Model basename without extension
MODEL_BASENAME="${DEFAULT_MODEL%.*}"

echo "Using $DEFAULT_MODEL as the default model (basename: $MODEL_BASENAME)"

# Build mode
if [ "$1" = "debug" ]; then
  echo "Building in DEBUG mode..."
  BUILD_TYPE="Debug"
  EMCC_FLAGS="-s ASSERTIONS=2 -s SAFE_HEAP=1 -s DEMANGLE_SUPPORT=1 -g4 --source-map-base /"
else
  echo "Building in RELEASE mode..."
  BUILD_TYPE="Release"
  EMCC_FLAGS="-O2"
fi

# Navigate to build directory
cd build-web

# Create a custom HTML shell template
echo "Creating body fragment template..."
cat >shell.html <<EOL
<canvas id="canvas" oncontextmenu="event.preventDefault()"></canvas>

{{{ SCRIPT }}}
EOL

# Replace model name in the HTML template
sed -i '' "s/\${MODEL_BASENAME}/$MODEL_BASENAME/g" shell.html

# Determine source files to compile
SOURCE_FILES="../simple-cube.cpp"

# If the model loader files exist, add them
if [ -f "../objloader.cpp" ]; then
  SOURCE_FILES="$SOURCE_FILES ../objloader.cpp"
fi

if [ -f "../gltfloader.cpp" ]; then
  SOURCE_FILES="$SOURCE_FILES ../gltfloader.cpp"
fi

if [ -f "../modelloader.cpp" ]; then
  SOURCE_FILES="$SOURCE_FILES ../modelloader.cpp"
fi

if [ -f "../spline.cpp" ]; then
  SOURCE_FILES="$SOURCE_FILES ../spline.cpp"
fi

if [ -f "../deform.cpp" ]; then
  SOURCE_FILES="$SOURCE_FILES ../deform.cpp"
fi

echo "Compiling source files: $SOURCE_FILES"

# Set up include paths
INCLUDE_PATHS="-I$EMSDK/upstream/emscripten/cache/sysroot/include"

if [ -n "$GLM_INCLUDE_DIR" ]; then
  INCLUDE_PATHS="$INCLUDE_PATHS -I$GLM_INCLUDE_DIR"
fi

if [ -n "$ASSIMP_INCLUDE_DIR" ]; then
  INCLUDE_PATHS="$INCLUDE_PATHS -I$ASSIMP_INCLUDE_DIR"
fi

if [ -d "../deps/tiny_gltf" ]; then
  INCLUDE_PATHS="$INCLUDE_PATHS -I../deps/tiny_gltf"
fi

# Compile the program with Emscripten
echo "Compiling program..."
emcc $SOURCE_FILES \
  -o ../web/model-renderer.html \
  $INCLUDE_PATHS \
  -s USE_GLFW=3 \
  -s GL_PREINITIALIZED_CONTEXT=1 \
  -s ASSERTIONS=1 \
  -s EXIT_RUNTIME=0 \
  -s WASM=1 \
  -s FULL_ES2=1 \
  -msimd128 \
  -s WASM_BIGINT=1 \
  -s MAX_WEBGL_VERSION=2 \
  -s MIN_WEBGL_VERSION=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=67108864 \
  -s GL_UNSAFE_OPTS=0 \
  -s GL_POOL_TEMP_BUFFERS=0 \
  -s GL_MAX_TEMP_BUFFER_SIZE=2097152 \
  -s DISABLE_DEPRECATED_FIND_EVENT_TARGET_BEHAVIOR=1 \
  -s "BINARYEN_EXTRA_PASSES='--one-caller-inline-max-function-size=1000'" \
  -s "EXPORTED_FUNCTIONS=['_main', '_loadModelFromJS']" \
  -s "EXPORTED_RUNTIME_METHODS=['ccall', 'cwrap']" \
  --shell-file shell.html \
  --preload-file ../assets@/assets \
  $EMCC_FLAGS

# If compilation was successful
if [ $? -eq 0 ]; then
  # Create JS glue code for model switching
  cat >../web/model-loader.js <<EOL
function startGameModule() {
    if (window.Module && window.Module.started) {
        console.warn("Module already started. Skipping re-initialization.");
        return;
    }

    var statusElement = document.getElementById('status');
    var infoElement = document.getElementById('info');

    window.Module = {
        preRun: [],
        postRun: [],
        print: function(text) {
            console.log(text);
            if (infoElement) infoElement.textContent = text;
        },
        printErr: function(text) {
            console.error(text);
        },
        canvas: (function() {
            var canvas = document.getElementById('canvas');
            if (canvas) {
                canvas.addEventListener("webglcontextlost", function(e) {
                    alert('WebGL context lost. You will need to reload the page.');
                    e.preventDefault();
                }, false);
            }
            return canvas;
        })(),
        setStatus: function(text) {
            if (!Module.setStatus.last) Module.setStatus.last = { time: Date.now(), text: '' };
            if (statusElement) statusElement.innerHTML = text;
            if (text === '') {
                if (statusElement) statusElement.style.display = 'none';
            } else {
                if (statusElement) statusElement.style.display = 'block';
            }
        },
        totalDependencies: 0,
        monitorRunDependencies: function(left) {
            this.totalDependencies = Math.max(this.totalDependencies, left);
            Module.setStatus(left
                ? 'Preparing... (' + (this.totalDependencies - left) + '/' + this.totalDependencies + ')'
                : 'All downloads complete.');
        },
        started: true // 👈 Important: Mark Module as started
    };

    Module.setStatus('Downloading...');

    window.onerror = function() {
        Module.setStatus('Exception thrown, see JavaScript console');
        Module.setStatus = function(text) {
            if (text) console.error('[post-exception status] ' + text);
        };
    };

    // ✅ Only inject script if not already present
    if (!document.getElementById('model-renderer-script')) {
        var script = document.createElement('script');
        script.src = '/model-renderer.js';
        script.id = 'model-renderer-script'; // 👈 Add ID
        document.body.appendChild(script);
    } else {
        console.warn("model-renderer.js already loaded. Skipping duplicate injection.");
    }
}
EOL

  echo "=============================================="
  echo "Build complete. To run the application:"
  echo "cd web"
  echo "python3 -m http.server 8000"
  echo ""
  echo "Then open http://localhost:8000/model-renderer.html in your browser"
  echo "=============================================="
else
  echo "Compilation failed!"
  exit 1
fi
