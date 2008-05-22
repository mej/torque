/*
*         OpenPBS (Portable Batch System) v2.3 Software License
* 
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
* 
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
* 
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
* 
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
* 
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
* 
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
* 
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
* 
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
* 
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
* 
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
* 
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information 
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
* 
* 7. DISCLAIMER OF WARRANTY
* 
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
* 
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
* 
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/
/*
 * svr_modify.c 
 *
 * Functions relating to the Modify Job Batch Requests.
 *
 * Included funtions are:
 *	
 *
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <sys/types.h>
#include "libpbs.h"
#include <signal.h>
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "resource.h"
#include "server.h"
#include "queue.h"
#include "credential.h"
#include "batch_request.h"
#include "job.h"
#include "work_task.h"
#include "pbs_error.h"
#include "log.h"
#include "svrfunc.h"

/* Global Data Items: */

extern attribute_def	    job_attr_def[];
extern char *msg_jobmod;
extern char *msg_manager;
extern char *msg_mombadmodify;
extern int   comp_resc_gt;
extern int   comp_resc_lt;
extern int   LOGLEVEL;





/*
 * post_modify_req - clean up after sending modify request to MOM
 */

static void post_modify_req(

  struct work_task *pwt)

  {
  struct batch_request *preq;

  svr_disconnect(pwt->wt_event);  /* close connection to MOM */

  preq = pwt->wt_parm1;

  preq->rq_conn = preq->rq_orgconn;  /* restore socket to client */

  if (preq->rq_reply.brp_code) 
    {
    sprintf(log_buffer,msg_mombadmodify,preq->rq_reply.brp_code);

    log_event(
      PBSEVENT_JOB, 
      PBS_EVENTCLASS_JOB,
      preq->rq_ind.rq_modify.rq_objname, 
      log_buffer);

    req_reject(preq->rq_reply.brp_code,0,preq,NULL,NULL);
    } 
  else
    {
    reply_ack(preq);
    }

  return;
  }  /* END post_modify_req() */





/*
 * req_modifyjob - service the Modify Job Request
 *
 * This request modifes a job's attributes.
 *
 * @see relay_to_mom() - child - routes change down to pbs_mom
 */

void req_modifyjob(

  struct batch_request *preq)

  {
  int		 bad = 0;
  int		 i;
  int		 newstate;
  int		 newsubstate;
  job		*pjob;
  svrattrl	*plist;
  resource_def	*prsd;
  int		 rc;
  int		 sendmom = 0;

  pjob = chk_job_request(preq->rq_ind.rq_modify.rq_objname, preq);

  if (pjob == NULL)
    {
    return;
    }

  /* cannot be in exiting or transit, exiting has already been checked */

  if (pjob->ji_qs.ji_state == JOB_STATE_TRANSIT) 
    {
    /* FAILURE */

    req_reject(PBSE_BADSTATE,0,preq,NULL,NULL);

    return;
    }

  /* If async modify, reply now; otherwise reply is handled later */

  if (preq->rq_type == PBS_BATCH_AsyModifyJob)
    {
        
    reply_ack(preq);

    preq->rq_noreply = TRUE; /* set for no more replies */
    }

  plist = (svrattrl *)GET_NEXT(preq->rq_ind.rq_modify.rq_attr);

  if (plist == NULL) 
    {	
    /* nothing to do */

    reply_ack(preq);

    return;
    }

  /* if job is running, special checks must be made */

  /* NOTE:  must determine if job exists down at MOM - this will occur if
            job is running, job is held, or job was held and just barely 
            released (ie qhold/qrls) */

  if ((pjob->ji_qs.ji_state == JOB_STATE_RUNNING) ||
      (pjob->ji_qs.ji_state == JOB_STATE_HELD) ||
     ((pjob->ji_qs.ji_state == JOB_STATE_QUEUED) && (pjob->ji_qs.ji_destin[0] != '\0')))
    {
    while (plist != NULL) 
      {
      /* is the attribute modifiable in RUN state ? */

      i = find_attr(job_attr_def,plist->al_name,JOB_ATR_LAST);

      if ((i < 0) ||
         ((job_attr_def[i].at_flags & ATR_DFLAG_ALTRUN) == 0))  
        {
        /* FAILURE */

        reply_badattr(PBSE_MODATRRUN,1,plist,preq);

        return;
        }

      /* NOTE:  only explicitly specified job attributes are routed down to MOM */

      if (i == (int)JOB_ATR_resource) 
        {
        /* is the specified resource modifiable while */
        /* the job is running                         */

        prsd = find_resc_def(
          svr_resc_def, 
          plist->al_resc,
          svr_resc_size);

        if (prsd == NULL) 
          {
          /* FAILURE */

          reply_badattr(PBSE_UNKRESC,1,plist,preq);

          return;
          }

        if ((prsd->rs_flags & ATR_DFLAG_ALTRUN) == 0) 
          {
          /* FAILURE */

          reply_badattr(PBSE_MODATRRUN,1,plist,preq);

          return;
          }

        sendmom = 1;
        }
      else if ((i == (int)JOB_ATR_checkpoint_name) || (i == (int)JOB_ATR_variables))
        {
        sendmom = 1;
        }

      plist = (svrattrl *)GET_NEXT(plist->al_link);
      }
    }    /* END if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING) */

  /* modify the job's attributes */

  bad = 0;

  plist = (svrattrl *)GET_NEXT(preq->rq_ind.rq_modify.rq_attr);

  rc = modify_job_attr(pjob,plist,preq->rq_perm,&bad);

  if (rc) 
    {
    /* FAILURE */

    reply_badattr(rc,bad,plist,preq);

    return;
    }

  /* Reset any defaults resource limit which might have been unset */

  set_resc_deflt(pjob,NULL);

  /* if job is not running, may need to change its state */

  if (pjob->ji_qs.ji_state != JOB_STATE_RUNNING) 
    {
    svr_evaljobstate(pjob,&newstate,&newsubstate,0);

    svr_setjobstate(pjob,newstate,newsubstate);
    } 
  else 
    {
    job_save(pjob,SAVEJOB_FULL);
    }

  sprintf(log_buffer,msg_manager,
    msg_jobmod,
    preq->rq_user, 
    preq->rq_host);

  LOG_EVENT(
    PBSEVENT_JOB, 
    PBS_EVENTCLASS_JOB, 
    pjob->ji_qs.ji_jobid,
    log_buffer);

  /* if a resource limit changed for a running job, send to MOM */

  if (sendmom)
    {
    if ((rc = relay_to_mom(
           pjob->ji_qs.ji_un.ji_exect.ji_momaddr,
           preq, 
           post_modify_req)))
      {
      req_reject(rc,0,preq,NULL,NULL);    /* unable to get to MOM */
      }

    return;
    }

  reply_ack(preq);

  return;
  }  /* END req_modifyjob() */





/*
 * modify_job_attr - modify the attributes of a job atomically
 *	Used by req_modifyjob() to alter the job attributes and by
 *	stat_update() [see req_stat.c] to update with latest from MOM
 */

int modify_job_attr(

  job	   *pjob,  /* I (modified) */
  svrattrl *plist, /* I */
  int	    perm,
  int	   *bad)   /* O */

  {
  int	     allow_unkn;
  long	     i;
  attribute  newattr[(int)JOB_ATR_LAST];
  attribute *pattr;
  int	     rc;

  if (pjob->ji_qhdr->qu_qs.qu_type == QTYPE_Execution)
    allow_unkn = -1;
  else
    allow_unkn = (int)JOB_ATR_UNKN;

  pattr = pjob->ji_wattr;

  /* call attr_atomic_set to decode and set a copy of the attributes */

  rc = attr_atomic_set(
    plist,        /* I */
    pattr,        /* I */
    newattr,      /* O */
    job_attr_def, /* I */
    JOB_ATR_LAST,
    allow_unkn,   /* I */
    perm,         /* I */
    bad);         /* O */

  /* if resource limits are being changed ... */

  if ((rc == 0) &&
      (newattr[(int)JOB_ATR_resource].at_flags & ATR_VFLAG_SET))
    {
    if ((perm & (ATR_DFLAG_MGWR | ATR_DFLAG_OPWR)) == 0) 
      {
      /* If job is running, only manager/operator can raise limits */

      if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING) 
        {
        if ((comp_resc2(
              &pjob->ji_wattr[(int)JOB_ATR_resource],
              &newattr[(int)JOB_ATR_resource],
              server.sv_attr[(int)SRV_ATR_QCQLimits].at_val.at_long,
              NULL) == -1) ||
            (comp_resc_lt != 0))
          {
          rc = PBSE_PERM;
          }
        }

      /* Also check against queue and system limits */

      if (rc == 0) 
        {
        rc = chk_resc_limits(
          &newattr[(int)JOB_ATR_resource],
          pjob->ji_qhdr,
          NULL);
        }
      }
    }    /* END if ((rc == 0) && ...) */

  /* special check on permissions for hold */

  if ((rc == 0) && 
      (newattr[(int)JOB_ATR_hold].at_flags & ATR_VFLAG_MODIFY)) 
    {
    i = newattr[(int)JOB_ATR_hold].at_val.at_long ^ 
          (pattr + (int)JOB_ATR_hold)->at_val.at_long;

    rc = chk_hold_priv(i,perm);
    }

  if (rc == 0) 
    {
    for (i = 0;i < JOB_ATR_LAST;i++) 
      {
      if (newattr[i].at_flags & ATR_VFLAG_MODIFY) 
        {
        if (job_attr_def[i].at_action) 
          {
          rc = job_attr_def[i].at_action(
            &newattr[i],
            pjob, 
            ATR_ACTION_ALTER);

          if (rc)
            break;
          }
        }
      }    /* END for (i) */

    if ((rc == 0) &&
       ((newattr[(int)JOB_ATR_userlst].at_flags & ATR_VFLAG_MODIFY) ||
        (newattr[(int)JOB_ATR_grouplst].at_flags & ATR_VFLAG_MODIFY)))
      {
      /* need to reset execution uid and gid */

      rc = set_jobexid(pjob,newattr,NULL);
      }
    }  /* END if (rc == 0) */

  if (rc != 0) 
    {
    for (i = 0;i < JOB_ATR_LAST;i++)
      job_attr_def[i].at_free(newattr + i);

    /* FAILURE */

    return(rc);
    }  /* END if (rc != 0) */

  /* OK, now copy the new values into the job attribute array */

  for (i = 0;i < JOB_ATR_LAST;i++) 
    {
    if (newattr[i].at_flags & ATR_VFLAG_MODIFY) 
      {
      if (LOGLEVEL >= 7)
        {
        sprintf(log_buffer,"attr %s modified",
          job_attr_def[i].at_name);

        LOG_EVENT(
          PBSEVENT_JOB, 
          PBS_EVENTCLASS_JOB, 
          pjob->ji_qs.ji_jobid,
          log_buffer);
        }

      job_attr_def[i].at_free(pattr + i);

      if ((newattr[i].at_type == ATR_TYPE_LIST) ||
          (newattr[i].at_type == ATR_TYPE_RESC)) 
        {
        list_move(
          &newattr[i].at_val.at_list, 
          &(pattr + i)->at_val.at_list);
        } 
      else 
        {
        *(pattr + i) = newattr[i];
        }

      (pattr + i)->at_flags = newattr[i].at_flags;
      }
    }    /* END for (i) */

  /* note, the newattr[] attributes are on the stack, they go away automatically */

  pjob->ji_modified = 1;

  return(0);
  }  /* END modify_job_attr() */


