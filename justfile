[group('esp')]
set-target:
    cd ./lilygo_platypus && idf.py set-target esp32s3

[group('esp')]
clean-esp:
    cd ./lilygo_platypus && rm -rf build/

[group('esp')]
flash: clean-esp set-target
    cd ./lilygo_platypus && idf.py build flash monitor

[group('esp')]
build-esp: clean-esp set-target
    cd ./lilygo_platypus && idf.py build

