/*
 * $Id$
 *
 * Copyright (c) 2008-2009 Nominet UK. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* 
 * keygend.c code implements the server_main
 * function needed by daemon.c
 *
 * The bit that makes the daemon do something useful
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>

#include "daemon.h"
#include "daemon_util.h"
#include "kaspaccess.h"

#include <uuid/uuid.h>
#include "ksm/datetime.h"
#include "ksm/string_util.h"

#include "libhsm.h"

int
server_init(DAEMONCONFIG *config)
{
    if (config == NULL) {
        log_msg(NULL, LOG_ERR, "Error, no config provided");
        exit(1);
    }

    /* set the default pidfile if nothing was provided on the command line*/
    if (config->pidfile == NULL) {
        config->pidfile = KEYGEN_PID;
    }

    return 0;
}

/*
 * Main loop of keygend server
 */
void
server_main(DAEMONCONFIG *config)
{
    DB_RESULT handle;
    DB_HANDLE dbhandle;
    int status = 0;
    int count = 0;
    int i = 0;
    uuid_t *uuid;
    char uuid_text[37];
    char *rightnow;
    DB_ID* ignore = 0;
    struct timeval tv;
    KSM_POLICY *policy;
    int result;
    hsm_ctx_t *ctx = NULL;
    hsm_key_t *key = NULL;
    
    if (config == NULL) {
        log_msg(NULL, LOG_ERR, "Error, no config provided");
        exit(1);
    }

    policy = (KSM_POLICY *)malloc(sizeof(KSM_POLICY));
    policy->signer = (KSM_SIGNER_POLICY *)malloc(sizeof(KSM_SIGNER_POLICY));
    policy->signature = (KSM_SIGNATURE_POLICY *)malloc(sizeof(KSM_SIGNATURE_POLICY));
    policy->ksk = (KSM_KEY_POLICY *)malloc(sizeof(KSM_KEY_POLICY));
    policy->zsk = (KSM_KEY_POLICY *)malloc(sizeof(KSM_KEY_POLICY));
    policy->denial = (KSM_DENIAL_POLICY *)malloc(sizeof(KSM_DENIAL_POLICY));
    policy->enforcer = (KSM_ENFORCER_POLICY *)malloc(sizeof(KSM_ENFORCER_POLICY));
    policy->name = (char *)calloc(KSM_NAME_LENGTH, sizeof(char));
    /* Let's check all of those mallocs, or should we use MemMalloc ? */
    if (policy->signer == NULL || policy->signature == NULL ||
            policy->ksk == NULL || policy->zsk == NULL || 
            policy->denial == NULL || policy->enforcer == NULL) {
        log_msg(config, LOG_ERR, "Malloc for policy struct failed\n");
        exit(1);
    }
    kaspSetPolicyDefaults(policy, NULL);
    
    while (1) {

        /* Read the config file */
        status = ReadConfig(config);
        if (status != 0) {
            log_msg(config, LOG_ERR, "Error reading config");
            exit(1);
        }

        log_msg(config, LOG_INFO, "Connecting to Database...");
        kaspConnect(config, &dbhandle);

        result = hsm_open(CONFIGFILE, hsm_prompt_pin, NULL);
        log_msg(config, LOG_INFO, "hsm_open result: %d\n", result);

        /* Read all policies */
        status = KsmPolicyInit(&handle, NULL);
        if (status == 0) {
            /* get the first policy */
            status = KsmPolicy(handle, policy);
            while (status == 0) {
                log_msg(config, LOG_INFO, "Policy %s found.", policy->name);
                if  (policy->shared_keys == 1 ) {
                    log_msg(config, LOG_INFO, "Key sharing is On");
                } else {
                    log_msg(config, LOG_INFO, "Key sharing is Off.");
                }
                /* Clear the policy struct */
                kaspSetPolicyDefaults(policy, policy->name);

                /* Read the parameters for that policy */
                status = kaspReadPolicy(policy);
								
                rightnow = DtParseDateTimeString("now");

                /* Find out how many ksk keys are needed */
                status = ksmKeyPredict(policy->id, KSM_TYPE_KSK, policy->shared_keys, config->keygeninterval, &count);
								/* TODO: check capacity of HSM will not be exceeded */
                /* Create the keys */
                for (i=count ; i > 0 ; i--){
                	if (policy->ksk->algorithm == 5 ) {
	                    key = hsm_generate_rsa_key(ctx, policy->ksk->sm_name, policy->ksk->bits);
	                    if (key) {
	                      log_msg(config, LOG_INFO,"Created key in HSM\n");
	                    } else {
	                      log_msg(config, LOG_ERR,"Error creating key in HSM\n");
	                      exit(1);
	                    }
	                    uuid = hsm_get_uuid(ctx, key);
	                    uuid_unparse(*uuid, uuid_text);
	                    status = KsmKeyPairCreate(policy->id, uuid_text, policy->ksk->sm, policy->ksk->bits, policy->ksk->algorithm, rightnow, ignore);
	                    log_msg(config, LOG_INFO, "Created KSK size: %i, alg: %i with uuid: %s in HSM: %s.", policy->ksk->bits, policy->ksk->algorithm, uuid_text, policy->ksk->sm_name);
	                } else {
	                  log_msg(config, LOG_ERR, "Key algorithm unsupported by libhsm.");
	                  exit(1);
	                }

                }
                /* Find out how many zsk keys are needed */
                status = ksmKeyPredict(policy->id, KSM_TYPE_ZSK, policy->shared_keys, config->keygeninterval, &count);
                /* TODO: check capacity of HSM will not be exceeded */
                for (i = count ; i > 0 ; i--){
                   if (policy->zsk->algorithm == 5 ) {	                   
	                     key = hsm_generate_rsa_key(ctx, policy->ksk->sm_name, policy->zsk->bits);
	                     if (key) {
	                       log_msg(config, LOG_INFO,"Created key in HSM\n");
	                     } else {
	                       log_msg(config, LOG_ERR,"Error creating key in HSM\n");
	                       exit(1);
	                     }
	                     uuid = hsm_get_uuid(ctx, key);
                       uuid_unparse(*uuid, uuid_text);
                       status = KsmKeyPairCreate(policy->id, uuid_text, policy->zsk->sm, policy->zsk->bits, policy->zsk->algorithm, rightnow, ignore);
                       log_msg(config, LOG_INFO, "Created ZSK size: %i, alg: %i with uuid: %s in HSM: %s.", policy->zsk->bits, policy->zsk->algorithm, uuid_text, policy->zsk->sm_name);
                    } else {
		                  log_msg(config, LOG_ERR, "unsupported key algorithm.");
		                  exit(1);
		                }
                }
                StrFree(rightnow);

                /* get next policy */
                status = KsmPolicy(handle, policy);
            }
        } else {
            log_msg(config, LOG_ERR, "Error querying KASP DB for policies.");
            exit(1);
        }
        DbFreeResult(handle);

        /* Disconnect from DB and HSMs in case we are asleep for a long time */
        log_msg(config, LOG_INFO, "Disconnecting from Database...");
        kaspDisconnect(&dbhandle);
        
        /*
         * Destroy HSM context
         */
        if (ctx) {
          hsm_destroy_context(ctx);
        }
  
        result = hsm_close();
        log_msg(config, LOG_INFO, "all done! hsm_close result: %d\n", result);
        
				if (config->term == 1 ){
					log_msg(config, LOG_INFO, "Exiting...");
					exit(0);
				}
        /* sleep for the key gen interval */
        tv.tv_sec = config->keygeninterval;
        tv.tv_usec = 0;
        log_msg(config, LOG_INFO, "Sleeping for %i seconds.",config->keygeninterval);
        select(0, NULL, NULL, NULL, &tv);

				if (config->term == 1 ){
					log_msg(config, LOG_INFO, "Exiting...");
					exit(0);
				}
				
    }
    log_msg(config, LOG_INFO, "Disconnecting from Database...");
    kaspDisconnect(&dbhandle);
    free(policy->name);
    free(policy->enforcer);
    free(policy->denial);
    free(policy->zsk);
    free(policy->ksk);
    free(policy->signature);
    free(policy->signer);
    free(policy);
}
