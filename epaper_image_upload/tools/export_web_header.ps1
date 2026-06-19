# 將可直接預覽的 web/*.html 轉成 Arduino 可使用的 header。
# 使用方式：在 epaper_image_upload 專案根目錄執行：
# powershell -ExecutionPolicy Bypass -File .\tools\export_web_header.ps1

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Export-HtmlHeader {
  param(
    [Parameter(Mandatory=$true)][string]$HtmlFile,
    [Parameter(Mandatory=$true)][string]$HeaderFile,
    [Parameter(Mandatory=$true)][string]$VariableName
  )

  $htmlPath = Join-Path $projectDir $HtmlFile
  $headerPath = Join-Path $projectDir $HeaderFile

  if (-not (Test-Path $htmlPath)) {
    throw "找不到 $htmlPath"
  }

  $html = [System.IO.File]::ReadAllText($htmlPath, $utf8NoBom)
  if ($html.Contains(')HTML"')) {
    throw "$HtmlFile 內容含有 Arduino raw string 結束標記，請改掉文字後再同步。"
  }

  $header = @"
// 本檔由 tools/export_web_header.ps1 產生，請不要手動修改。
// 修改網頁請編輯 $HtmlFile，再執行該工具同步到 Arduino 韌體。
#pragma once
#include <Arduino.h>

const char $VariableName[] PROGMEM = R"HTML(
$html
)HTML";
"@

  [System.IO.File]::WriteAllText($headerPath, $header, $utf8NoBom)
  Write-Host "已同步 $HtmlFile -> $HeaderFile"
}

Export-HtmlHeader -HtmlFile "web\index.html" -HeaderFile "web_index.h" -VariableName "INDEX_HTML"
Export-HtmlHeader -HtmlFile "web\nfc.html" -HeaderFile "nfc_index.h" -VariableName "NFC_INDEX_HTML"
Export-HtmlHeader -HtmlFile "web\config.html" -HeaderFile "config_index.h" -VariableName "CONFIG_INDEX_HTML"
