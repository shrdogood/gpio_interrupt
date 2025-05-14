#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Please specify GPIO group and GPIO offset\n");
        exit(1);
    }
    int gpio_offset = 0;
    const char *event_mode = "both";
    struct gpiod_chip *chip = NULL;
    struct gpiod_line *line = NULL;

    char gpiochip[64] = {0};
    snprintf(gpiochip, sizeof(gpiochip), "gpiochip%d", atoi(argv[1]));
    gpio_offset = atoi(argv[2]); // 指定管脚

    if(argc > 3) {
        if(strcmp(argv[3], "rising") == 0 || strcmp(argv[3], "r") == 0){
            event_mode = "rising";
            printf("Rising interrupt monitoring...\n");
        } else if(strcmp(argv[3], "falling") == 0 || strcmp(argv[3], "f") == 0) {
            event_mode = "falling";
            printf("Falling interrupt monitoring...\n");
        } else {
            event_mode = "both";
            printf("Both Edge monitoring..\n");
        }
    } else {
        printf("Both Edge monitoring...\n");
    }

    chip = gpiod_chip_open_by_name(gpiochip);
    if(!chip){
        printf("Can't open gpio chip %s\n", gpiochip);
        return EXIT_FAILURE;
    }

    line = gpiod_chip_get_line(chip, gpio_offset);
    if(!line){
        perror("Error gpio get line");
        gpiod_chip_close(chip);
        return EXIT_FAILURE;
    }

    int ret; // Interrupt Event
    if(strcmp(event_mode, "rising") == 0) {
        ret = gpiod_line_request_rising_edge_events(line, "gpiod_demo_rising");
    } else if (strcmp(event_mode, "falling") == 0) {
        ret = gpiod_line_request_falling_edge_events(line, "gpiod_demo_falling");
    } else {
        ret = gpiod_line_request_both_edges_events(line, "gpiod_demo_both");
    }

    if(ret < 0) {
        perror("Failed alloc GPIO interrupt");
        gpiod_chip_close(chip);
        return EXIT_FAILURE;
    }

    while(1) {
        struct gpiod_line_event event;
        struct timespec ts = {1, 0};
        // 阻塞等待
        ret = gpiod_line_event_wait(line, &ts);
        if(ret < 0) {
            perror("Event Error!");
            break;
        } else if(ret == 0){
            continue; // Wait for next evnet
        }

        if(gpiod_line_event_read(line, &event) < 0) {
            perror("Reading Event failed");
            break;
        }

        printf("GPIO %d %d :%s\n", atoi(argv[1]), gpio_offset, event.event_type == GPIOD_LINE_EVENT_RISING_EDGE ? "Rising Event" : "Falling Event");
    }

    if(line) {
        gpiod_line_release(line);
    }
    if(chip){
        gpiod_chip_close(chip);
    }

    return 0;
}