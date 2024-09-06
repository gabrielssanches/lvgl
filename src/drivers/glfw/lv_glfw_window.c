/**
 * @file lv_glfw_window.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_glfw_window_private.h"
#if LV_USE_OPENGLES
#include <stdlib.h>
#include "../../core/lv_refr.h"
#include "../../stdlib/lv_string.h"
#include "../../core/lv_global.h"
#include "../../display/lv_display_private.h"
#include "../../indev/lv_indev.h"
#include "../../lv_init.h"
#include "../../misc/lv_area_private.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "lv_opengles_driver.h"
#include "lv_opengles_texture.h"
#include <stdio.h>

static void window_update_handler(lv_timer_t * t);
static void proc_mouse(lv_glfw_window_t * window);
static void indev_read_cb(lv_indev_t * indev, lv_indev_data_t * data);
static void _lv_glfw_keyboard_read(lv_indev_t * drv, lv_indev_data_t * data);

static bool glfw_inited;
static lv_ll_t glfw_window_ll;

lv_glfw_window_t * lv_glfw_window_create(int32_t hor_res, int32_t ver_res, bool use_mouse_indev)
{
    if(glfw_inited) {
        return 0;
    }

    lv_ll_init(&glfw_window_ll, sizeof(lv_glfw_window_t));


    lv_glfw_window_t * window = lv_ll_ins_tail(&glfw_window_ll);
    LV_ASSERT_MALLOC(window);
    if(window == NULL) return NULL;
    lv_memzero(window, sizeof(*window));

    /* Create window with graphics context */
    lv_glfw_window_t * existing_window = lv_ll_get_head(&glfw_window_ll);
    window->hor_res = hor_res;
    window->ver_res = ver_res;
    lv_ll_init(&window->textures, sizeof(lv_glfw_texture_t));
    window->use_indev = use_mouse_indev;
    

    glfw_inited = true;

    return window;
}

void lv_glfw_window_delete(lv_glfw_window_t * window)
{
    if(window->use_indev) {
        lv_glfw_texture_t * texture;
        LV_LL_READ(&window->textures, texture) {
            lv_indev_delete(texture->indev);
        }
    }
    lv_ll_clear(&window->textures);
    lv_ll_remove(&glfw_window_ll, window);
    lv_free(window);
}

lv_glfw_texture_t * lv_glfw_window_add_texture(lv_glfw_window_t * window, unsigned int texture_id, int32_t w, int32_t h)
{
    lv_glfw_texture_t * texture = lv_ll_ins_tail(&window->textures);
    LV_ASSERT_MALLOC(texture);
    if(texture == NULL) return NULL;
    lv_memzero(texture, sizeof(*texture));
    texture->window = window;
    texture->texture_id = texture_id;
    lv_area_set(&texture->area, 0, 0, w - 1, h - 1);
    texture->opa = LV_OPA_COVER;

    if(window->use_indev) {
        lv_display_t * texture_disp = lv_opengles_texture_get_from_texture_id(texture_id);
        if(texture_disp != NULL) {
            lv_indev_t * indev = lv_indev_create();
            if(indev == NULL) {
                lv_ll_remove(&window->textures, texture);
                lv_free(texture);
                return NULL;
            }
            texture->indev = indev;
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(indev, indev_read_cb);
            lv_indev_set_driver_data(indev, texture);
            lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
            lv_indev_set_display(indev, texture_disp);


            texture->lv_indev_keyboard = lv_indev_create();
            if(texture->lv_indev_keyboard == NULL) {
                lv_ll_remove(&window->textures, texture);
                lv_free(texture);
                return NULL;
            }
            lv_indev_set_type(texture->lv_indev_keyboard, LV_INDEV_TYPE_KEYPAD);
            lv_indev_set_read_cb(texture->lv_indev_keyboard, _lv_glfw_keyboard_read);
            lv_indev_set_driver_data(texture->lv_indev_keyboard, texture);
            lv_indev_set_mode(texture->lv_indev_keyboard, LV_INDEV_MODE_EVENT);
            lv_indev_set_display(texture->lv_indev_keyboard, texture_disp);
        }
    }

    return texture;
}

void lv_glfw_texture_remove(lv_glfw_texture_t * texture)
{
    if(texture->indev != NULL) {
        lv_indev_delete(texture->indev);
    }
    lv_ll_remove(&texture->window->textures, texture);
    lv_free(texture);
}

void lv_glfw_texture_set_x(lv_glfw_texture_t * texture, int32_t x)
{
    lv_area_set_pos(&texture->area, x, texture->area.y1);
}

void lv_glfw_texture_set_y(lv_glfw_texture_t * texture, int32_t y)
{
    lv_area_set_pos(&texture->area, texture->area.x1, y);
}

void lv_glfw_texture_set_opa(lv_glfw_texture_t * texture, lv_opa_t opa)
{
    texture->opa = opa;
}

lv_indev_t * lv_glfw_texture_get_mouse_indev(lv_glfw_texture_t * texture)
{
    return texture->indev;
}

// USE THIS FOR RAYLIB?
static void window_update_handler(lv_timer_t * t)
{
    LV_UNUSED(t);

    lv_glfw_window_t * window;

    glfwPollEvents();

    /* delete windows that are ready to close */
    window = lv_ll_get_head(&glfw_window_ll);
    while(window) {
        lv_glfw_window_t * window_to_delete = window->closing ? window : NULL;
        window = lv_ll_get_next(&glfw_window_ll, window);
        if(window_to_delete) {
            glfwSetWindowShouldClose(window_to_delete->window, GLFW_TRUE);
            lv_glfw_window_delete(window_to_delete);
        }
    }

    /* render each window */
    LV_LL_READ(&glfw_window_ll, window) {
        //glfwMakeContextCurrent(window->window);
        lv_opengles_viewport(0, 0, window->hor_res, window->ver_res);
        lv_opengles_render_clear();

        /* render each texture in the window */
        lv_glfw_texture_t * texture;
        LV_LL_READ(&window->textures, texture) {
            /* if the added texture is an LVGL opengles texture display, refresh it before rendering it */
            lv_display_t * texture_disp = lv_opengles_texture_get_from_texture_id(texture->texture_id);
            if(texture_disp != NULL) {
                lv_refr_now(texture_disp);
            }

            lv_opengles_render_texture(texture->texture_id, &texture->area, texture->opa, window->hor_res, window->ver_res);
        }

        /* Swap front and back buffers */
        //glfwSwapBuffers(window->window);
    }
}

void lv_glfw_window_mouse_button(lv_glfw_window_t * lv_window, int button, int action, int mods)
{
    LV_UNUSED(mods);
    if(button == GLFW_MOUSE_BUTTON_LEFT) {
        lv_window->mouse_last_state = action == GLFW_PRESS ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        proc_mouse(lv_window);
    }
}

void lv_glfw_window_mouse_move(lv_glfw_window_t * lv_window, double xpos, double ypos)
{
    lv_window->mouse_last_point.x = (int32_t)xpos;
    lv_window->mouse_last_point.y = (int32_t)ypos;
    proc_mouse(lv_window);
}

static void proc_mouse(lv_glfw_window_t * window)
{
    /* mouse activity will affect the topmost LVGL display texture */
    lv_glfw_texture_t * texture;
    LV_LL_READ_BACK(&window->textures, texture) {
        if(lv_area_is_point_on(&texture->area, &window->mouse_last_point, 0)) {
            /* adjust the mouse pointer coordinates so that they are relative to the texture */
            texture->indev_last_point.x = window->mouse_last_point.x - texture->area.x1;
            texture->indev_last_point.y = window->mouse_last_point.y - texture->area.y1;
            texture->indev_last_state = window->mouse_last_state;
            lv_indev_read(texture->indev);
            break;
        }
    }
}

static void indev_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    lv_glfw_texture_t * texture = lv_indev_get_driver_data(indev);
    data->point = texture->indev_last_point;
    data->state = texture->indev_last_state;
}

static void _lv_glfw_keyboard_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    lv_glfw_texture_t * texture = lv_indev_get_driver_data(indev);
    data->key = texture->indev_key;
    data->state = texture->indev_key_state;
}

#endif /*LV_USE_OPENGLES*/
