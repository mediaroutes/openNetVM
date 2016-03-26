/*********************************************************************
 *                     openNetVM
 *       https://github.com/sdnfv/openNetVM
 *
 *  Copyright 2015 George Washington University
 *            2015 University of California Riverside
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  test_flow_dir.c - an example using onvm_flow_dir_* APIs.
 ********************************************************************/

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/queue.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>

#include <rte_common.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_malloc.h>
#include <rte_memzone.h>
#include <rte_memory.h>

#include "onvm_nflib.h"
#include "onvm_pkt_helper.h"
#include "onvm_flow_table.h"
#include "onvm_sc_common.h"
#include "onvm_flow_dir.h"
#include "onvm_sc_mgr.h"

#define NF_TAG "test_flow_dir"
#define NEXT_SERVICE_ID 2

/* Struct that contains information about this NF */
struct onvm_nf_info *nf_info;

/* number of package between each print */
static uint32_t print_delay = 1000000;

static uint32_t destination;

/*
 * Print a usage message
 */
static void
usage(const char *progname) {
        printf("Usage: %s [EAL args] -- [NF_LIB args] -- -d <destination> -p <print_delay>\n\n", progname);
}

/*
 * Parse the application arguments.
 */
static int
parse_app_args(int argc, char *argv[], const char *progname) {
        int c;

        while ((c = getopt(argc, argv, "p:")) != -1) {
                switch (c) {
                case 'p':
                        print_delay = strtoul(optarg, NULL, 10);
                        break;
                case '?':
                        usage(progname);
                        if (optopt == 'p')
                                RTE_LOG(INFO, APP, "Option -%c requires an argument.\n", optopt);
                        else if (isprint(optopt))
                                RTE_LOG(INFO, APP, "Unknown option `-%c'.\n", optopt);
                        else
                                RTE_LOG(INFO, APP, "Unknown option character `\\x%x'.\n", optopt);
                        return -1;
                default:
                        usage(progname);
                        return -1;
                }
        }
        return optind;
}

/*
 * This function displays stats. It uses ANSI terminal codes to clear
 * screen when called. It is called from a single non-master
 * thread in the server process, when the process is run with more
 * than one lcore enabled.
 */
static void
do_stats_display(struct rte_mbuf* pkt) {
        const char clr[] = { 27, '[', '2', 'J', '\0' };
        const char topLeft[] = { 27, '[', '1', ';', '1', 'H', '\0' };
        static int pkt_process = 0;
        struct ipv4_hdr* ip;

        pkt_process += print_delay;

        /* Clear screen and move to top left */
        printf("%s%s", clr, topLeft);

        printf("PACKETS\n");
        printf("-----\n");
        printf("Port : %d\n", pkt->port);
        printf("Size : %d\n", pkt->pkt_len);
        printf("N°   : %d\n", pkt_process);
        printf("\n\n");

        ip = onvm_pkt_ipv4_hdr(pkt);
        if (ip != NULL) {
                onvm_pkt_print(pkt);
        } else {
                printf("No IP4 header found\n");
        }
}

static int
packet_handler(struct rte_mbuf* pkt, struct onvm_pkt_meta* meta) {
        static uint32_t counter = 0;        
	struct onvm_flow_entry *flow_entry = NULL;
	int ret;	

	if (++counter == print_delay) {
                do_stats_display(pkt);
                counter = 0;
        }
	
	/* The system may crash when receiving a packet with hash.rss=0 
	 * we should find out the reason.
	 */
	if (pkt->hash.rss == 0) {
		meta->action = ONVM_NF_ACTION_DROP;
		meta->destination = 0;
		return 0;
	}

	ret = onvm_flow_dir_get_pkt(pkt, &flow_entry);
	if (ret >= 0) {
        	meta->action = onvm_sc_next_action(flow_entry->sc, pkt);
        	meta->destination = onvm_sc_next_destination(flow_entry->sc, pkt);
	}
	else {
		ret = onvm_flow_dir_add_pkt(pkt, &flow_entry);
		memset(flow_entry, 0, sizeof(struct onvm_flow_entry));
		flow_entry->sc = onvm_sc_create();
		onvm_sc_append_entry(flow_entry->sc, ONVM_NF_ACTION_TONF, NEXT_SERVICE_ID);
		//onvm_sc_print(flow_entry->sc);
	}
       	return 0;
}

int main(int argc, char *argv[]) {
        int arg_offset;

        const char *progname = argv[0];

        if ((arg_offset = onvm_nf_init(argc, argv, NF_TAG)) < 0)
                return -1;
        argc -= arg_offset;
        argv += arg_offset;
	destination = nf_info->service_id + 1;

        if (parse_app_args(argc, argv, progname) < 0)
                rte_exit(EXIT_FAILURE, "Invalid command-line arguments\n");
	
	/* Map the sdn_ft table */	
	onvm_flow_dir_nf_init();	
        
	onvm_nf_run(nf_info, &packet_handler);
        printf("If we reach here, program is ending");
        
	return 0;
}
