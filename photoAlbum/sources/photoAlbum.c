#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>


/**** RGB888 颜色定义 ****/
typedef struct rgb888_type {
 unsigned char blue;
 unsigned char green;
 unsigned char red;
} __attribute__ ((packed)) rgb888_t;

/**** BMP文件头数据结构 ****/
typedef struct {
    unsigned char type[2];      //文件类型
    unsigned int size;          //文件大小
    unsigned short reserved1;   //保留字段1
    unsigned short reserved2;       //保留字段2
    unsigned int offset;        //到位图数据的偏移量
} __attribute__ ((packed)) bmp_file_header;

/**** 位图信息头数据结构 ****/
typedef struct {
    unsigned int size;          //位图信息头大小
    int width;                  //图像宽度
    int height;                 //图像高度
    unsigned short planes;          //位面数
    unsigned short bpp;         //像素深度 
    unsigned int compression;   //压缩方式
    unsigned int image_size;    //图像大小
    int x_pels_per_meter;       //像素/米
    int y_pels_per_meter;       //像素/米 
    unsigned int clr_used;
    unsigned int clr_omportant;
} __attribute__ ((packed)) bmp_info_header;

/**** 静态全局变量 ****/
static struct fb_fix_screeninfo fb_fix;
static struct fb_var_screeninfo fb_var;
static unsigned short *screen_base = NULL;        // 映射后的显存基地址
static unsigned int pages = 0;                                      // 当前显示图片页号


/**** 全局变量 ****/
const   char * path = "/dev/input/event0";              // 触摸屏点击事件
/* 能被点击的位置大小，用于区分菜单栏位置和显示位置 */
unsigned int canTouchX = 1024;
unsigned int canTouchy = 600;


/**
 * @brief 
 * 显示一张BMP格式图片，兼容16位、24位、32位像素深度的图片
 * 1、读取图片文件头，判断是否为BMP格式
 * 2、读取图片信息头，
 *      获取并打印：文件大小、位图数据的偏移量、位图信息头大小、图像分辨率、像素深度
 * 3、申请行缓冲区保存图片每行数据
 * 4、比较图片大小和窗口大小，得出每行显示的字节数
 * 5、读取图像数据显示到LCD：
 *      分辨视图是否倒转
 *              倒转：偏移到最后一行数据，自下往上逐行读取图片数据并拷贝到帧缓冲区
 *              正向：直接从图片数据头逐行读取并拷贝
 * @param path BMP图片路径
 * @return int 返回0执行成功，返回-1为失败
 */
static int show_bmp_image(const char *path)
{
    bmp_file_header file_h;     // 文件头
    bmp_info_header info_h;     // 信息头
    unsigned short *line_buf = NULL;        //行缓冲区
    unsigned long line_bytes;       //BMP图像一行的字节的数
    unsigned int min_h, min_bytes;      // 显示区域高，每行字节数
    unsigned short *base = screen_base;     // 当前显示行
    unsigned int bytes_per_pixel = fb_var.bits_per_pixel / 8;       // 每个像素字节数 = 像素深度 / 8
    unsigned int width = fb_fix.line_length / bytes_per_pixel;      // 可视区域行像素数 = 一行的字节数 / 每像素字节数
    unsigned int line_length = bytes_per_pixel * fb_var.xres;       // 可视区域行字节数 = 每个像素字节数 * 可视区域X分辨率
    unsigned int show_length;       // 每行显示的数据长度，可适应不同像素深度的图片
    int bmpFd = -1;
    int j;


    if (0 > (bmpFd = open(path, O_RDONLY))) {
        perror("path error");
        return -1;
    }

    // 读取文件头
    if(sizeof(file_h) !=
     read(bmpFd, &file_h, sizeof(file_h))){
         perror("read bmp_file_header");
         close(bmpFd);
         return -1;
     }

    //  判断是否为BMP格式
    if(0 != memcmp(file_h.type, "BM", 2)){
        fprintf(stderr, "it's not a BMP file\n");
        close(bmpFd);
        return -1;
    }else{
        // fprintf(stdout, "open pic success!!\n");
    }

    /* 读取位图信息头 */
    if (sizeof(bmp_info_header) !=
        read(bmpFd, &info_h, sizeof(bmp_info_header))) {
        perror("read error");
        close(bmpFd);
        return -1;
    }

   /* 打印信息 */
//    printf("分辨率: %d*%d\n"
//            "像素深度 bpp: %d\n"
//            "一行的字节数: %d\n"
//            "像素格式: R<%d %d> G<%d %d> B<%d %d>\n",
//            fb_var.xres, fb_var.yres, fb_var.bits_per_pixel,
//            fb_fix.line_length,
//            fb_var.red.offset, fb_var.red.length,
//            fb_var.green.offset, fb_var.green.length,
//            fb_var.blue.offset, fb_var.blue.length);
//     printf("文件大小: %d\n"
//          "位图数据的偏移量: %d\n"
//          "位图信息头大小: %d\n"
//          "图像分辨率: %d*%d\n"
//          "像素深度: %d\n", file_h.size, file_h.offset,
//          info_h.size, info_h.width, info_h.height,
//          info_h.bpp);
    /* 将文件读写位置移动到图像数据开始处 */
    if (-1 == lseek(bmpFd, file_h.offset, SEEK_SET)) {
        perror("lseek error");
        close(bmpFd);
        return -1;
    }

    // 图片每行的字节数 = 图片每行的像素数 * 像素深度 / 8位
    line_bytes = info_h.width * info_h.bpp / 8;
    // 申请行缓冲区保存图片每行数据
    line_buf = malloc(line_bytes);
    if (NULL == line_buf) {
        fprintf(stderr, "malloc line_buf error\n");
        close(bmpFd);
        return -1;
    }
//     printf("line_buf size:%ld\n",line_bytes);


    // 比较图片大小和窗口大小，得出每行显示的字节数
    if (line_length > line_bytes)
        min_bytes = line_bytes;
    else
        min_bytes = line_length;


    if(info_h.height > 0){
        // 倒立视图
        // printf("倒立视图!\n");

        if(info_h.height < fb_var.yres){
            // 图片高度 < 窗口高度
            min_h = info_h.height;      //高度限制为图片高度
            base += width * (min_h - 1);       // 定位到图片左下角

        }else{

            min_h = fb_var.yres;
            lseek(bmpFd, (info_h.height - fb_var.yres) * line_bytes, SEEK_CUR);     // 定位到屏幕能显示的最大范围
            base += width * (min_h - 1);       // 换行
        }

        // printf("min_h:%d,\tmin_byte:%d\n\n\n",min_h,min_bytes);

        // 显示图片
        for(j = min_h; j > 0; base -= width, j--){
            read(bmpFd, line_buf, line_bytes);
            memcpy(base, line_buf, min_bytes);
        }

    }else{
        // 正向视图
        // printf("正向视图!\n");
        int temp = 0 - info_h.height;       //负数转成正数

        if(temp < fb_var.yres){
            min_h = temp;
        }else{
            min_h = fb_var.yres;
        }

        // printf("temp:%d\tfb_var.yres:%d\n",temp,fb_var.yres);
        // printf("min_h:%d,\tmin_byte:%d\n",min_h,min_bytes);
        
        // 显示图片
        for(j = 0; j < min_h; j++, base += width){
            read(bmpFd, line_buf, line_bytes);
            memcpy(base, line_buf, min_bytes);
        }
    }

    /* 关闭文件、函数返回 */
    close(bmpFd);
    free(line_buf);
    return 0;
}


int main(int argc, char *argv[])
{
        printf("argc = %d;\n",argc);
        // 触摸事件参数
        int touchFd = -1;                                          // 文件描述符
        struct input_event event;               // 事件结构体
        int size;                                                   // 读取的文件大小
        int count = 0, x, y;
        
        unsigned int screen_size;       // 帧缓冲区大小
        int screenFd = -1;
        int j;


        // 1、校验传参：是否携带图片路径
        if(argc < 2){
                perror("argc length");
                exit(-1);
        }
        pages = argc - 1;                       // 获取图片数

        /* 打开framebuffer设备 */
        if(0 > (screenFd = open("/dev/fb0",O_RDWR))){
                perror("open fb0");
                exit(EXIT_FAILURE);
        }

        /* 获取参数信息 */
        ioctl(screenFd, FBIOGET_VSCREENINFO, &fb_var);
        ioctl(screenFd, FBIOGET_FSCREENINFO, &fb_fix);
        // printf("\nfb_var.xres:%d,\tfb_var.yres:%d,\tfb_var.xres_virtual%d,\tfb_var.yres_virtual%d,\tbpp:%d\n\n",
        //         fb_var.xres,fb_var.yres, fb_var.xres_virtual, fb_var.yres_virtual, fb_var.bits_per_pixel);

        /* 将显示缓冲区映射到进程地址空间 */
        // line_length：一行的字节数
        screen_size = fb_fix.line_length * fb_var.yres;
        // void *mmap(void *addr, size_t length, int prot, int flags,int fd, off_t offset);
        screen_base = mmap(NULL,screen_size, PROT_WRITE, MAP_SHARED, screenFd, 0);
        if (MAP_FAILED == (void *)screen_base) {
                perror("mmap error");
                close(screenFd);
                exit(EXIT_FAILURE);
        }

        /* 显示BMP图片 */
        memset(screen_base, 0xff, screen_size);
        show_bmp_image(argv[1]);                // 首先显示第一张图片
        /* 幻灯片播放形式 */
        // for (int i = 1; i < argc;)
        // {
        //         show_bmp_image(argv[i]);
        //         sleep(2);
        //         if(i == argc - 1){
        //         i = 1;
        //         }else{
        //         i += 1;
        //         }
        // }

        /* 触摸换图 */
        // 打开触摸屏事件
        touchFd = open(path, O_RDONLY);
        if(touchFd < 0){
                perror("open /dev/input/event");
        }

        // 获取点击位置
        while(1){

                // 读取事件
                size = read(touchFd, &event, sizeof(event));

                // 如果获取到事件
                if(size == sizeof(struct input_event)){
                        // 点击事件
                        if(event.type == EV_ABS && event.code==ABS_X)
                        {
                                // printf("x=%d\n",event.value);
                                x = event.value;
                                count++;
                        }
                        else if(event.type == EV_ABS && event.code==ABS_Y)
                        {
                                // printf("y=%d\n\n\n",event.value);
                                y = event.value;
                                count++;
                        }
                        if(count == 2){
                                printf("(%d,%d)",x,y);
                                // 点击右边屏幕
                                if(x > canTouchX / 2){
                                        printf("下一张图片\n\n");
                                        pages = (pages + 1) % (argc - 1);       // 获取即将显示的图片页号

                                        memset(screen_base, 0xff, screen_size);         // 清屏，防止图像混淆
                                        show_bmp_image(argv[pages + 1]);                // 显示图片
                                }
                                else if(x <= canTouchX / 2){
                                        printf("上一张图片\n\n");
                                        pages = (pages + argc - 2) % (argc - 1);

                                        memset(screen_base, 0xff, screen_size);
                                        show_bmp_image(argv[pages + 1]);
                                }
                                // break;
                                // 防止多次点击
                                sleep(0.5);
                                count = 0;
                        }
                }
        }


        // 取消映射
        munmap(screen_base,screen_size);
        /* 退出 */
        close(touchFd);
        close(screenFd);
        exit(0);

}









