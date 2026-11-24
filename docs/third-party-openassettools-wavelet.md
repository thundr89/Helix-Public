# OpenAssetTools wavelet decoder adaptation

The Huffman codewords and reconstruction algorithm in
src/cod4/cod4_iwi_wavelet.c are adapted to C99 from OpenAssetTools:
https://github.com/Laupetin/OpenAssetTools/blob/2826174cf74dd6a94bcc0a1b96c1967aa3d8d760/src/ObjImage/Image/IwiWaveletDecoder.cpp

Copyright OpenAssetTools contributors. Distributed under GNU GPL version 3;
see COPYING.OpenAssetTools.GPL-3.0.txt. The adaptation uses per-call state,
validates resource limits and returns a standard TGA through the existing asset
plugin interface. It is built only into the GPL adapter, not the Helix engine.
