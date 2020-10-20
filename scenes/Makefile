all : \
    ../dist/place.pnct \
    ../dist/place.scene \
    ../dist/place.w \

../dist/place.scene : place.blend export-scene.py
    "C:\Program Files\Blender Foundation\Blender 2.90\blender.exe" --background --python export-scene.py -- "place.blend:Collection" "../dist/place.scene"

../dist/place.pnct : place.blend export-meshes.py
    "C:\Program Files\Blender Foundation\Blender 2.90\blender.exe" --background --python export-meshes.py -- "place.blend:Collection" "../dist/place.pnct" 

../dist/place.w : place.blend export-walkmeshes.py
    "C:\Program Files\Blender Foundation\Blender 2.90\blender.exe" --background --python export-walkmeshes.py -- "place.blend:Collection" "../dist/place.w" 