# Vulkan 1.4 Compute Shader Demo

Vulkan 1.4 API を用いて、Push Constants で定数データをシェーダーへ送り、コンピュートシェーダーで計算した結果（SSBO）をCPUへ読み戻してコンソールに出力するサンプルプログラムです。

## 特徴
- **Vulkan 1.4**: `apiVersion = VK_API_VERSION_1_4` を指定。
- **Push Constants**: CPU側から定数パラメータ（`multiplier`, `offset`）を低オーバーヘッドでGPUに送信。
- **SSBO (Shader Storage Buffer Object)**: GPUの計算結果を書き込み、ホスト可視メモリ（`HOST_VISIBLE` / `HOST_COHERENT`）経由でCPUから直接読み出し。
- **自動シェーダーコンパイル**: CMakeビルド時に `glslc` を使って `compute.comp` から SPIR-V (`compute.spv`) を自動コンパイル。

## 必要要件
- Windows 10 / 11
- Vulkan SDK 1.4 以降
- CMake 3.20 以降
- C++17 対応コンパイラ（Visual Studio 2022 / MSVC など）
- Vulkan 1.4 対応 GPU / グラフィックスドライバ

## ビルド手順

```powershell
# プロジェクトルートで実行
cmake -B build -S .
cmake --build build --config Release
```

## 実行手順

```powershell
.\build\Release\Vulkan14Compute.exe
```

## 出力例

```text
====================================================
 Vulkan 1.4 Compute Shader Demo
====================================================

[INFO] シェーダーバイナリを発見しました: .../compute.spv
[INFO] Validation Layer (VK_LAYER_KHRONOS_validation) を有効化しました。
[INFO] Vulkan 1.4 インスタンスを作成しました。
[INFO] 選択されたGPU: NVIDIA GeForce RTX 4070 Ti SUPER
[INFO] サポートされているAPIバージョン: 1.4.329

[INFO] シェーダーへ定数データを送信:
       - multiplier: 10
       - offset:     7

[INFO] GPUでコンピュートシェーダーを実行中...
[INFO] 計算が正常に完了しました。

====================================================
 GPUコンピュートシェーダーからの計算結果 (全 64 件)
 式: results[i] = i * multiplier (10) + offset (7)
====================================================
Thread [ 0] :    7    Thread [ 1] :   17    Thread [ 2] :   27    Thread [ 3] :   37
...
Thread [60] :  607    Thread [61] :  617    Thread [62] :  627    Thread [63] :  637
====================================================

[INFO] すべてのリソースを正常に解放しました。
```
