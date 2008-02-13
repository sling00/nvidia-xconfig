/*
 * nvidia-xconfig: A tool for manipulating XConfig files,
 * specifically for use by the NVIDIA Linux graphics driver.
 *
 * Copyright (C) 2004 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *      Free Software Foundation, Inc.
 *      59 Temple Place - Suite 330
 *      Boston, MA 02111-1307, USA
 *
 *
 * tree.c - this source file contains routines for printing an
 * XConfig in a tree format, rooted at the ServerLayout section.
 *
 * Note that the tree is just printed to stdout (not through an
 * ncurses ui) so we can just call printf directly here.
 *
 * TODO: there are many fields that we are not yet printed
 */


#include <stdio.h>

#include "nvidia-xconfig.h"



/*
 * print_options()
 */

static void print_options(XConfigOptionPtr options, int indents)
{
    XConfigOptionPtr opt;
    int i;

    for (opt = options; opt; opt = opt->next) {
        printf("        |");
        for (i = 1; i < indents; i++) printf("       |");
        printf("--> Option \"%s\"", opt->name);
        if (opt->val) printf(" \"%s\"", opt->val);
        printf("\n");
    }
} /* print_options() */



/*
 * print_range()
 */

static void print_range(parser_range* range, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (i > 0) printf(", ");
        printf("%.1f-%.1f", range[i].lo, range[i].hi);
    }
    printf("\n");
    
} /* print_range() */



/*
 * print_monitor() 
 */

static void print_monitor(XConfigMonitorPtr monitor)
{
    XConfigModeLinePtr modeline;

    printf("        |       |--> Monitor \"%s\"\n", monitor->identifier);
    printf("        |       |       |\n");

    if (monitor->vendor) {
        printf("        |       |       |--> VendorName \"%s\"\n",
               monitor->vendor);
    }
    
    if (monitor->modelname) {
        printf("        |       |       |--> ModelName \"%s\"\n",
               monitor->modelname);
    }

    printf("        |       |       |--> HorizSync  ");
    print_range(monitor->hsync, monitor->n_hsync);

    printf("        |       |       |--> VertRefresh  ");
    print_range(monitor->vrefresh, monitor->n_vrefresh);

    for (modeline = monitor->modelines; modeline; modeline = modeline->next) {
        printf("        |       |       |--> Modeline \"%s\" ...\n",
               modeline->identifier);
    }

    print_options(monitor->options, 3);

    printf("        |       |\n");
    
} /* print_monitor() */



/*
 * print_device()
 */

static void print_device(XConfigDevicePtr device)
{
    printf("        |       |--> Device \"%s\"\n", device->identifier);

    if (device->driver) {
        printf("        |       |       |--> Driver \"%s\"\n",
               device->driver);
    }
    if (device->vendor) {
        printf("        |       |       |--> VendorName \"%s\"\n",
               device->vendor);
    }
    if (device->board) {
        printf("        |       |       |--> BoardName \"%s\"\n",
               device->board);
    }
    if (device->busid) {
        printf("        |       |       |--> BusID \"%s\"\n",
               device->busid);
    }
    if (device->screen >= 0) {
        printf("        |       |       |--> Screen \"%d\"\n",
               device->screen);
    }

    print_options(device->options, 3);
    
    printf("        |       |\n");
    
} /* print_device() */



/*
 * print_modes()
 */

static void print_modes(XConfigScreenPtr screen)
{
    XConfigDisplayPtr display;
    XConfigModePtr mode;

    printf("        |       |--> Modes");

    for (display = screen->displays; display; display = display->next) {
        if (display->depth == screen->defaultdepth) {
            for (mode = display->modes; mode; mode = mode->next) {
                printf(" \"%s\"", mode->mode_name);
            }
            break;
        }
    }

    printf("\n");

} /* print_modes() */



/*
 * print_screen()
 */

static void print_screen(XConfigScreenPtr screen)
{
    printf("        |--> Screen \"%s\"\n", screen->identifier);
    printf("        |       |\n");
    print_monitor(screen->monitor);

    print_device(screen->device);
    
    print_options(screen->options, 2);

    printf("        |       |--> DefaultColorDepth %d\n",
           screen->defaultdepth);
    print_modes(screen);

    printf("        |\n");
    
} /* print_screen() */



/*
 * print_input()
 */

static void print_input(XConfigInputrefPtr inputRef)
{
    XConfigInputPtr input = inputRef->input;
    
    printf("        |--> InputDevice \"%s\"\n", input->identifier);
    printf("        |       |\n");
    printf("        |       |--> Driver \"%s\"\n", input->driver);
    
    print_options(input->options, 2);
    print_options(inputRef->options, 2);

    printf("        |\n");
    
} /* print_input() */



/*
 * print_layout()
 */

static void print_layout(XConfigLayoutPtr layout)
{
    XConfigAdjacencyPtr adj;
    XConfigInputrefPtr input;

    printf("\n");
    printf("    ServerLayout \"%s\"\n", layout->identifier);
    printf("        |\n");
    
    for (adj = layout->adjacencies; adj; adj = adj->next) {
        print_screen(adj->screen);
    }

    for (input = layout->inputs; input; input = input->next) {
        print_input(input);
    }

    print_options(layout->options, 1);
    
} /* print_layout() */



/*
 * print_server_flags()
 */

static void print_server_flags(XConfigPtr config)
{
    if (!config->flags || !config->flags->options) return;

    printf("\n");
    printf("    ServerFlags\n");
    printf("        |\n");
    
    print_options(config->flags->options, 1);
    
    printf("\n");

} /* print_server_flags() */



/*
 * print_tree()
 */

int print_tree(Options *op, XConfigPtr config)
{
    XConfigLayoutPtr layout;
    
    if (!config) {
        printf("Unable to locate/open XConfig file.\n");
        return FALSE;
    }
    
    /*
     * either use the requested layout or loop over all the layouts in
     * the file
     */
    
    if (op->layout) {
        layout = xconfigFindLayout(op->layout, config->layouts);
        if (!layout) {
            printf("Unable to find layout \"%s\".\n", op->layout);
            return FALSE;
        }
        print_layout(layout);
    } else {
        for (layout = config->layouts; layout; layout = layout->next) {
            print_layout(layout);
        }
    }
    
    printf("\n");

    print_server_flags(config);

    return TRUE;

} /* print_tree() */
