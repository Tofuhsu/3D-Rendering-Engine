#!/bin/sh
# Download the third-party models and HDR images used for demos and benchmarks.
# Every file is pinned by SHA-256; licences and credits are in
# assets/third_party/SOURCES.md. Downloads are cached in assets/third_party/_downloads.
set -e
cd "$(dirname "$0")/../assets/third_party"
mkdir -p _downloads gltf/SciFiHelmet hdri

sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }

# fetch <url> <file under _downloads> <sha256>
fetch() {
  if [ -f "_downloads/$2" ] && [ "$(sha256 "_downloads/$2")" = "$3" ]; then
    return
  fi
  echo "downloading $2"
  mkdir -p "$(dirname "_downloads/$2")"
  curl -fsSL -A "3D-Rendering-Engine fetch_assets" -o "_downloads/$2.part" "$1"
  if [ "$(sha256 "_downloads/$2.part")" != "$3" ]; then
    echo "SHA-256 mismatch for $2" >&2
    rm -f "_downloads/$2.part"
    exit 1
  fi
  mv "_downloads/$2.part" "_downloads/$2"
}

MCG=https://casual-effects.com/g3d/data10
KHR=https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models
PH=https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/2k

fetch $MCG/research/model/sportsCar/sportsCar.zip sportsCar.zip 83dac92ab6ed1b902d61b8e7cbfc9bcf7b95afa2a6070e6a3ec1cc16d5585046
fetch $MCG/common/model/mori_knob/mori_knob.zip mori_knob.zip 7ef9d756891dc9df972109d7767105d7dea8d8c55171a6462033ae36e257238b
fetch $MCG/common/model/CornellBox/CornellBox.zip CornellBox.zip f27eaf47afec74b5236ce3095ac02d4256e8ac8610bfdb5a53560c67c2e0d8f1

fetch $KHR/Lantern/glTF-Binary/Lantern.glb Lantern.glb a79458c4b02d695187a952f23a63b8bf278e7bc3d316a3c2a314f2d6974181f1
fetch $KHR/NormalTangentMirrorTest/glTF-Binary/NormalTangentMirrorTest.glb NormalTangentMirrorTest.glb 31ce5f3c873fc55531a17ccacf001e3d1d482a508695aa826fa79cc196e0ce78
fetch $KHR/DamagedHelmet/glTF-Binary/DamagedHelmet.glb DamagedHelmet.glb a1e3b04de97b11de564ce6e53b95f02954a297f0008183ac63a4f5974f6b32d8
fetch $KHR/SciFiHelmet/glTF/SciFiHelmet.gltf SciFiHelmet/SciFiHelmet.gltf 4b9e64c5337ddaba5b44821d76786bcad3b5b78f877cf034ec6163dc8baca35a
fetch $KHR/SciFiHelmet/glTF/SciFiHelmet.bin SciFiHelmet/SciFiHelmet.bin 6a7af42896359f8e0d9ba65260cbb7b1a1d36aa787439d3775a48aba1f9a25ba
fetch $KHR/SciFiHelmet/glTF/SciFiHelmet_AmbientOcclusion.png SciFiHelmet/SciFiHelmet_AmbientOcclusion.png 95823e77b0a6e45308b53be4fdf1b7a7b6ac085c190dc1c5ef47097d0e35eb2f
fetch $KHR/SciFiHelmet/glTF/SciFiHelmet_BaseColor.png SciFiHelmet/SciFiHelmet_BaseColor.png bda08dd029775ea0964d5721b779a639da091222eaa750233ba34e01e3eeace2
fetch $KHR/SciFiHelmet/glTF/SciFiHelmet_MetallicRoughness.png SciFiHelmet/SciFiHelmet_MetallicRoughness.png f38f025af3201697679e2a04ba4cf01b4506e68ce4e0e0f09c268a7dfb80b2c3
fetch $KHR/SciFiHelmet/glTF/SciFiHelmet_Normal.png SciFiHelmet/SciFiHelmet_Normal.png 437c034b60900b503bb41be3328011ab3b7602c8454f8eebcab98929ea25fe99

fetch $PH/studio_small_08_2k.hdr studio_small_08_2k.hdr 6e677b7421f4a14f0844dece04243c4ab3f4bf1a05bf4bb79e29368b3ecc7746
fetch $PH/venice_sunset_2k.hdr venice_sunset_2k.hdr cbfac020ee17ab36016ccc7fbe9020b828587b9864af6ecda0a93089914889a6

unzip -q -o _downloads/sportsCar.zip -d sports_car
unzip -q -o _downloads/mori_knob.zip -d mori_knob
unzip -q -o _downloads/CornellBox.zip -d cornell_box
cp _downloads/*.glb gltf/
cp _downloads/SciFiHelmet/* gltf/SciFiHelmet/
cp _downloads/*.hdr hdri/
echo "assets ready in assets/third_party"
