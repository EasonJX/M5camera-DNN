# M5Stack Camera with psram base in [esp32-camera](https://github.com/espressif/esp32-camera.git)


## General Information

M5camra には OV2640 image sensorsが付いています。
このイメージセンサーはなかなかの優れもので、映像をJPEGで出すこともできれば、BMPでそのまま出力することもできるらしい。
今回は、JPEGで出力した画像で文字認識を行う文字認識を実習したいと思います。

# 使いかた。

以下の手順で開発を行いましょう。

## 1.ローカルPC上でESP-IDFのコマンドプロンプトを起動する

## 2.GitHuB上から矢野先生のファイルをクローンしてくる。
 
 > gti init
 
 > git clone  https://github.com/ShouheiYano2020/M5camera-DNN.git

### 2.1. 学内でプロクシー環境下で行っているひとは、上記コマンドが失敗するのでプロクシを設定してから再度上記コマンドを行う
 【gitのプロクシ設定】 git config --global http.proxy http://[proxy]:[port] 
 
 > git config --global http://proxy.st.nagaoka-ct.ac.jp:8080
 
 こんな感じです。

### 2.2 寮で認証付きプロクシー環境下で行っている人は、
  【認証付きgitプロクシー】git config --global http.proxy http://<ユーザ名>:<パスワード>@<プロキシサーバのアドレス>:<プロキシサーバのポート番号>】
  
  > git config --global http:proxy  http://syano:XYZ!!123@proxy.st.nagaoka-ct.ac.jp:8080
  
 この例では、ユーザ名 syano 、パスワードがXYZ!!123となっていることを想定しています。

## 3. 環境をクリーンする。
 
 > idf.py fullclean
 
## 4. ビルドする

 > idf.py build
 
## 5. フラッシュする

 > idf.py flash -p com4
 
 M5Cameraが接続されたポートが、USBポートでポート４となっていることを想定しています。
 
## 6. モニターする

>idf.py monitor
 
## 補足.　カメラの映像を確認する

パソコン・携帯電話等のWifiへの接続が可能な機器で、m5stack-cam-4 というSSIDに接続します。
パスワードは必要ないです。

ブラウザを開き、IPアドレス 192.168.4.1を入力し接続します。

画像が見えます。

M5カメラの前のレンズを２回転ほど左に回してピンとを合わせます。初期状態では数ｍ先にピンとが合うようになっていますが
これを２，３ｃｍで会うように調整します。
  


Last modify 2020.06.08
  
  
