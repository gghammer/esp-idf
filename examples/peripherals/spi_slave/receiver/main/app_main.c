/* SPI Slave example, receiver (uses SPI Slave driver to communicate with sender)

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/igmp.h"

#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "soc/rtc_periph.h"
#include "esp32/rom/cache.h"
#include "driver/spi_slave.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"




/*
SPI receiver (slave) example.

This example is supposed to work together with the SPI sender. It uses the standard SPI pins (MISO, MOSI, SCLK, CS) to
transmit data over in a full-duplex fashion, that is, while the master puts data on the MOSI pin, the slave puts its own
data on the MISO pin.

This example uses one extra pin: GPIO_HANDSHAKE is used as a handshake pin. After a transmission has been set up and we're
ready to send/receive data, this code uses a callback to set the handshake pin high. The sender will detect this and start
sending a transaction. As soon as the transaction is done, the line gets set low again.
*/

/*
Pins in use. The SPI Master can use the GPIO mux, so feel free to change these if needed.
*/
#define GPIO_HANDSHAKE 2
#define GPIO_MOSI 12
#define GPIO_MISO 13
#define GPIO_SCLK 15
#define GPIO_CS 14


//Called after a transaction is queued and ready for pickup by master. We use this to set the handshake line high.
void my_post_setup_cb(spi_slave_transaction_t *trans) {
    WRITE_PERI_REG(GPIO_OUT_W1TS_REG, (1<<GPIO_HANDSHAKE));
}

//Called after transaction is sent/received. We use this to set the handshake line low.
void my_post_trans_cb(spi_slave_transaction_t *trans) {
    WRITE_PERI_REG(GPIO_OUT_W1TC_REG, (1<<GPIO_HANDSHAKE));
}

//Main application
void app_main()
{
    int n=0;
    esp_err_t ret;

    printf("Hammer Test");
    //Configuration for the SPI bus
    spi_bus_config_t buscfg={
        .mosi_io_num=GPIO_MOSI,
        .miso_io_num=GPIO_MISO,
        .sclk_io_num=GPIO_SCLK
    };

    //Configuration for the SPI slave interface
    spi_slave_interface_config_t slvcfg={
        .mode=0,
        .spics_io_num=GPIO_CS,
        .queue_size=3,
        .flags=0,
        .post_setup_cb=my_post_setup_cb,
        .post_trans_cb=my_post_trans_cb
    };

    //Configuration for the handshake line
    gpio_config_t io_conf={
        .intr_type=GPIO_INTR_DISABLE,
        .mode=GPIO_MODE_OUTPUT,
        .pin_bit_mask=(1<<GPIO_HANDSHAKE)
    };

    //Configure handshake line as output
    gpio_config(&io_conf);
    //Enable pull-ups on SPI lines so we don't detect rogue pulses when no master is connected.
    gpio_set_pull_mode(GPIO_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_SCLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_CS, GPIO_PULLUP_ONLY);

    //Initialize SPI slave interface
    ret=spi_slave_initialize(HSPI_HOST, &buscfg, &slvcfg, 1);
    assert(ret==ESP_OK);

    char sendbuf[129]="";
    char recvbuf[129]="";
    memset(recvbuf, 0, 33);
    spi_slave_transaction_t t;
    memset(&t, 0, sizeof(t));

    while(1) {
        //Clear receive buffer, set send buffer to something sane
        memset(recvbuf, 0xA5, 129);
        sprintf(sendbuf, "This is the receiver, sending data for transmission number %04d.", n);

        //Set up a transaction of 128 bytes to send/receive
        t.length=128*8;
        t.tx_buffer=sendbuf;
        t.rx_buffer=recvbuf;
        /* This call enables the SPI slave interface to send/receive to the sendbuf and recvbuf. The transaction is
        initialized by the SPI master, however, so it will not actually happen until the master starts a hardware transaction
        by pulling CS low and pulsing the clock etc. In this specific example, we use the handshake line, pulled up by the
        .post_setup_cb callback that is called as soon as a transaction is ready, to let the master know it is free to transfer
        data.
        */
        ret=spi_slave_transmit(HSPI_HOST, &t, portMAX_DELAY);

        //spi_slave_transmit does not return until the master has done a transmission, so by here we have sent our data and
        //received data from the master. Print it.
        printf("Received: %s\n", recvbuf);
        n++;
    }

}


