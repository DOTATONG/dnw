/* dnw2 linux main file. This depends on libusb.
 * License: GPL
 */
#include <stdio.h>
#include <usb.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

//JZ2440
#define JZ2440_VENDOR_ID 0x5345
#define JZ2440_PRODUCT_ID 0x1234
#define JZ2440_RAM_BASE 0x30000000
#define JZ2440_EP_IN 0x81
#define JZ2440_EP_OUT 0x03

//download address
#define RAM_BASE JZ2440_RAM_BASE
#define VENDOR_ID JZ2440_VENDOR_ID
#define PRODUCT_ID JZ2440_PRODUCT_ID

//Endpoint
#define EP_IN JZ2440_EP_IN
#define EP_OUT JZ2440_EP_OUT

struct usb_dev_handle *open_port()
{
    struct usb_bus *busses, *bus;

    usb_init();
    usb_find_busses();
    usb_find_devices();

    busses = usb_get_busses();
    for (bus = busses; bus; bus = bus->next)
    {
        struct usb_device *dev;
        for (dev = bus->devices; dev; dev = dev->next)
        {
            //printf("idVendor:0x%x\t,ipProduct:0x%x\n", dev->descriptor.idVendor, dev->descriptor.idProduct);

            if (VENDOR_ID == dev->descriptor.idVendor && PRODUCT_ID == dev->descriptor.idProduct)
            {
                printf("Target usb device found!\n");
                struct usb_dev_handle *hdev = usb_open(dev);
                if (!hdev)
                {
                    perror("Cannot open device");
                }
                else
                {
                    if (0 != usb_claim_interface(hdev, 0))
                    {
                        perror("Cannot claim interface");
                        usb_close(hdev);
                        hdev = NULL;
                    }
                }
                return hdev;
            }
        }
    }

    printf("Target usb device not found!\n");

    return NULL;
}

static u_int16_t ace_csum(const unsigned char *data, u_int32_t len)
{
    u_int16_t csum = 0;
    int j;

    for (j = 0; j < len; j++)
    {
        csum += data[j];
    }

    return csum;
}

unsigned char *prepare_write_buf(char *filename, unsigned int *len, unsigned long load_addr)
{
    unsigned char *write_buf = NULL;
    struct stat fs;

    int fd = open(filename, O_RDONLY);
    if (-1 == fd)
    {
        perror("Cannot open file");
        return NULL;
    }
    if (-1 == fstat(fd, &fs))
    {
        perror("Cannot get file size");
        goto error;
    }
    write_buf = (unsigned char *)malloc(fs.st_size + 10);
    if (NULL == write_buf)
    {
        perror("malloc failed");
        goto error;
    }

    if (fs.st_size != read(fd, write_buf + 8, fs.st_size))
    {
        perror("Reading file failed");
        goto error;
    }

    printf("Filename : %s\n", filename);
    printf("Filesize : %lld bytes\n", fs.st_size);

    *((u_int32_t *)write_buf) = load_addr;           //download address
    *((u_int32_t *)write_buf + 1) = fs.st_size + 10; //download size;
    *len = fs.st_size + 10;

    // calculate checksum value
    u_int16_t csum = ace_csum(write_buf + 8, fs.st_size);
    // printf("CheckSum=%d\n", (int)csum);
    *(write_buf + fs.st_size + 8) = csum & 0xff;
    *(write_buf + fs.st_size + 9) = (csum >> 8) & 0xff;
    // printf("write_buf[%d]=%.2x\n", (int)fs.st_size + 8, write_buf[(int)fs.st_size + 8]);
    // printf("write_buf[%d]=%.2x\n", (int)fs.st_size + 9, write_buf[(int)fs.st_size + 9]);
    return write_buf;

error:
    if (fd != -1)
        close(fd);
    if (NULL != write_buf)
        free(write_buf);
    fs.st_size = 0;
    return NULL;
}

int main(int argc, char *argv[])
{
    unsigned load_addr = RAM_BASE;
    char *path = NULL;
    int c;

    //处理命令行参数
    while ((c = getopt(argc, argv, "a:h")) != EOF)
        switch (c)
        {
        case 'a':
            load_addr = strtol(optarg, NULL, 16);
            continue;
        case '?':
        case 'h':
        default:
        usage:
            printf("Usage: dwn2 [-a load_addr] <filename>\n");
            printf("Default load address: 0x40008000\n");
            return 1;
        }
    if (optind < argc)
        path = argv[optind];
    else
        goto usage;
    printf("load address: 0x%08X\n", load_addr);

    //打开设备文件
    struct usb_dev_handle *hdev = open_port();
    if (!hdev)
    {
        return 1;
    }

    //准备写入空间
    unsigned int len = 0;
    unsigned char *write_buf = prepare_write_buf(path, &len, load_addr);
    if (NULL == write_buf)
        return 1;
    //写入数据
    unsigned int remain = len;
    unsigned int towrite;
    printf("Writing data ...\n");
    while (remain)
    {
        towrite = remain > 512 ? 512 : remain;
        if (towrite != usb_bulk_write(hdev, EP_OUT, (char *)write_buf + (len - remain), towrite, 3000))
        {
            perror("usb_bulk_write failed");
            break;
        }
        remain -= towrite;
        printf("\r%d%%\t %d bytes     ", (len - remain) * 100 / len, len - remain);
        fflush(stdout);
    }
    if (0 == remain)
        printf("Done!\n");
    return 0;
}
