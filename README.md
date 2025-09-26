# ESP32-S3 的 OV7670 驱动测试

手上有个 无 FIFO 的 OV7670 摄像头, 尝试使用 ESP32-S3 N16R8 的 核心开发版 识别并驱动.

> 起初以为很简单, 直接用 esp32-camera 自带驱动即可, 结果发现没那么简单, 经过一遍遍的尝试 和 查找相关信息, 

> 最终算是勉强跑起来, 只是 FPS 低的可怜 (<= 8), 很难有大的提升

能正常识别的话, 项目启动后, 会启动 web server,  浏览器访问 单片机的 ip 地址, 就能看到摄像头的实时预览

![运行效果](./preview.png)

## 摄像头无法识别问题

出现如下错误, 无法正常识别OV7670的话, 大概率是因 I2C 通讯不太稳定, 需要修改驱动代码.

```log
E (6584) camera: Detected camera not supported.
E (6588) camera: Camera probe failed with error 0x106(ESP_ERR_NOT_SUPPORTED)
E (6596) gdma: gdma_disconnect(309): no peripheral is connected to the channel
E (6604) camera_test: Camera init failed with error 0x106
```

### 驱动修改

修改 [esp32-camera](https://github.com/espressif/esp32-camera) 的 `SCCB_Read` 函数

1. 项目根目录新建 `components` 目录 (已存在则忽略)

2. 移动 `managed_components/espressif__esp32-camera` 到 `components` 目录下 (即 `components/espressif__esp32-camera`)

3. 修改 `components/espressif__esp32-camera/driver/sccb-ng.c` 文件

```c
uint8_t SCCB_Read(uint8_t slv_addr, uint8_t reg)
{
    i2c_master_dev_handle_t dev_handle = *(get_handle_from_address(slv_addr));

    uint8_t tx_buffer[1];
    uint8_t rx_buffer[1];

    tx_buffer[0] = reg;
    
    // esp_err_t ret = i2c_master_transmit_receive(dev_handle, tx_buffer, 1, rx_buffer, 1, TIMEOUT_MS);

    // if (ret != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "SCCB_Read Failed addr:0x%02x, reg:0x%02x, data:0x%02x, ret:%d", slv_addr, reg, rx_buffer[0], ret);
    // }

    // 串行通信，改善稳定性
    esp_err_t ret = ESP_FAIL;

    // 先发送寄存器地址
    ret = i2c_master_transmit(dev_handle, tx_buffer, 1, SCCB_FREQ);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SCCB_Read transmit Failed addr:0x%02x, reg:0x%02x, data:0x%02x, ret:%d", slv_addr, reg, rx_buffer[0], ret);
    } else {
      // 再读取数据
      ret = i2c_master_receive(dev_handle, rx_buffer, 1, SCCB_FREQ);

      if (ret != ESP_OK)
      {
          ESP_LOGE(TAG, "SCCB_Read receive Failed addr:0x%02x, reg:0x%02x, data:0x%02x, ret:%d", slv_addr, reg, rx_buffer[0], ret);
      }
    }

    return rx_buffer[0];
}

4. 修改完 `idf.py build` 重新编译即可

```