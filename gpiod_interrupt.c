#include <gpiod_interrupt>

//开启GPIO边沿触发中断，并循环检测
int gpiod_interrupt(char *chip_number=NULL, char *line_number=NULL, char *event_mode=NULL){
    struct gpiod_chip *chip = NULL;
    struct gpiod_line *line = NULL;
    char gpiochip[64] = {0};
    int gpio_offset = 0;
    int ret = 0;

    if(!chip_number || !line_number || !event_mode){
        perror("the param Error!");
        return EXIT_FAILURE;
    }

    snprintf(gpiochip, sizeof(gpiochip), "gpiochip%d", atoi(chip_number));
    gpio_offset = atoi(line_number);
    
    // printf
    if(strcmp(event_mode, "rising") == 0){
        printf("Rising interrupt monitoring...\n");
    } else if(strcmp(event_mode, "falling") == 0){
        printf("Falling interrupt monitoring...\n");
    } else if(strcmp(event_mode, "both") == 0){
        printf("Both Edge monitoring..\n");
    } else{
        perror("event_mode Error!");
        return EXIT_FAILURE;
    }

    chip = gpiod_chip_open_by_name(gpiochip);  // 开启GPIO chip
    if(!chip){
        perror("Can't open gpio chip %s\n", gpiochip);
        return EXIT_FAILURE;
    }

    line = gpiod_chip_get_line(chip, gpio_offset);  // 开启GPIO line
    if(!line){
        perror("Error gpio get line");
        gpiod_chip_close(chip);
        return EXIT_FAILURE;
    }

    // 开启GPIO边沿触发中断
    if(event_mode[0] == 'r'){
        ret = gpiod_line_request_rising_edge_events(line, "gpiod_demo_rising");
    } else if (event_mode[0] == 'f') {
        ret = gpiod_line_request_falling_edge_events(line, "gpiod_demo_falling");
    } else {
        ret = gpiod_line_request_both_edges_events(line, "gpiod_demo_both");
    }

    if(ret < 0) {
        perror("Failed alloc GPIO interrupt");
        gpiod_line_release(line);
        gpiod_chip_close(chip);
        return EXIT_FAILURE;
    }

    while(1) {
        struct gpiod_line_event event;
        struct timespec ts = {1, 0};
        // 阻塞等待  等待边沿触发中断
        ret = gpiod_line_event_wait(line, &ts);
        if(ret < 0) {
            perror("Event Error!");
            break;
        } else if(ret == 0){
            continue; // Wait for next event
        }

        //触发中断、执行中断处理函数
        if(gpiod_line_event_read(line, &event) < 0) {
            perror("Reading Event failed");
            break;
        }
        printf("GPIO %d %d :%s\n", atoi(chip_number), gpio_offset, event.event_type == GPIOD_LINE_EVENT_RISING_EDGE ? "Rising Event" : "Falling Event");
        
        gpio_interrupt_handler();
    }


    if(line) {
        gpiod_line_release(line);
    }
    if(chip){
        gpiod_chip_close(chip);
    }

    return 0;
}

// 当GPIO中断触发时，执行PA关闭的相关步骤
void gpio_interrupt_handler(void)
{
    // Step2.1 配置TDD PA控制为关闭
    configure_tdd_pa_ctrl_off();

    // Step2.2 配置CLGC和DPD暂停
    configure_clgc_and_dpd_pause();

    // Step2.3 数字域掐0
    configure_dpd_zero();
}

//在关PA状态下检测
void start_pa_process(void)
{
    // Step1: 检测关PA状态，若所有PAP触发指示消失，进入Step2
    if (check_all_pap_trigger_off()) {
        // Step2: 检测所有通道PAP的关PA触发指示是否全部消失
        if (check_all_pap_trigger_off()) {
            // Step3: 配置TDD输出的PA控制为正常切换模式
            configure_tdd_pa_ctrl_normal();

            // Step4: 配置数字域数据恢复
            configure_dpd_recover();

            // Step5: 配置CLGC开启并进行DPD复位
            configure_clgc_and_dpd_reset();

            // Step6: 配置PAP为不可恢复模式
            configure_pap_unrecoverable();
        }
    }
}