/*-
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*****************************************************************************
 * File: fmpool.c
 *
 * This is a modfied version of the original Berkley code for
 * manipulating a memory pool. This version however is not 
 * compatible with the original Berkley version.
 *
 *****************************************************************************/ 

#ifdef RCSID
static char RcsId[] = "@(#)$Revision$";
#endif

/* $Id$ */

#define	__FMPOOLINTERFACE_PRIVATE
#include "fmpool.h"

#if defined(hpux) || defined(__hpux) || defined(__hpux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#define getrusage(a, b)  syscall(SYS_GETRUSAGE, a, b)
#endif /* hpux */


/* Private routines */
#ifndef USE_INLINE
static BKT *mpool_bkt   __P((MPOOL *));
static BKT *mpool_look  __P((MPOOL *, pageno_t));
#endif
static int  mpool_write __P((MPOOL *, BKT *));

/*-----------------------------------------------------------------------------
NAME
   mpool_open -- Open a memory pool on the given file
USAGE
  MPOOL *mpool_open(key, fd, pagesize, maxcache)
  void   *key;       IN: byte string used as handle to share buffers 
  int    fd;         IN: seekable file descriptor 
  pageno_t pagesize;   IN: size in bytes of the pages to break the file up into 
  pageno_t maxcache;   IN: maximum number of pages to cache at any time 

RETURNS
   A memory pool cookie if successful else NULL
DESCRIPTION
   Initialize a memory pool. Currently we handle only file descriptors
   and not file pointers or other types of file handles.
   We try to find the length of the file using either the stat() calls
   or seeking to the end of the file and getting the offset. We take
   special note of the size of the lastpage when the file size is not even
   multiple of page sizes.

 NOTE: We don't have much use for the page in/out filters as we rely
       on the interfaces above us to fill the page and we allow the user
       to arbitrarily change the pagesize from one invocation to another.
       This deviates from the original Berkely implemntation.

 COMMENT: the key string byte for sharing buffers is not implemented
---------------------------------------------------------------------------- */
MPOOL *
mpool_open(key, fd, pagesize, maxcache)
  void       *key;     /* byte string used as handle to share buffers */
  fmp_file_t fd;       /* seekable file handle */
  pageno_t     pagesize; /* size in bytes of the pages to break the file up into */
  pageno_t     maxcache; /* maximum number of pages to cache at any time */
{
#ifdef _POSIX_SOURCE
  struct stat sb; /* file status info */
#endif
  struct _lhqh *lhead = NULL; /* head of an entry in list hash chain */
  MPOOL        *mp    = NULL; /* MPOOL cookie */
  L_ELEM       *lp    = NULL;
  int          len    = 0;    /* file length */
  int          ret    = RET_SUCCESS;
  int          entry;         /* index into hash table */
  pageno_t       pageno;


  /*
   * Get information about the file.
   * We don't currently handle pipes, although we should.
   */
#ifdef _POSIX_SOURCE
  /* Lets get file information using "fstat()" */
  if (fstat(fd, &sb))
    return (NULL);
  if (!S_ISREG(sb.st_mode)) 
    {
      errno = ESPIPE;
      ret = RET_ERROR;
      goto done;
    }

  /* Set the size of the file, pagesize and max # of pages to cache */
  len = sb.st_size;
  if(pagesize == 0)
    pagesize = (pageno_t)sb.st_blksize;
  if (maxcache == 0)
    maxcache = (pageno_t)DEF_MAXCACHE;
#ifdef STAT_DEBUG
    (void)fprintf(stderr,"mpool_open: sb.st_blksize=%d\n",sb.st_blksize);
#endif
#else  /* !_POSIX_SOURCE */
  /* Find the length of the file the really cheesy way! */
  if (FMPI_SEEKEND(fd) == FAIL)
    {
      ret = RET_ERROR;
      goto done;
    }
  if ((len = FMPI_TELL(fd)) == FAIL)
    {
      ret = RET_ERROR;
      goto done;
    }

  /* Set the pagesize and max # of pages to cache */
  if(pagesize == 0)
    pagesize = (pageno_t)DEF_PAGESIZE;
  if (maxcache == 0)
      maxcache = (pageno_t)DEF_MAXCACHE;
#ifdef STAT_DEBUG
    (void)fprintf(stderr,"mpool_open: no fstat(),pagesize=%u\n",pagesize);
#endif
#endif /* !_POSIX_SOURCE */

  /* Allocate and initialize the MPOOL cookie. */
  if ((mp = (MPOOL *)calloc(1, sizeof(MPOOL))) == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }
  CIRCLEQ_INIT(&mp->lqh);
  for (entry = 0; entry < HASHSIZE; ++entry)
    {
      CIRCLEQ_INIT(&mp->hqh[entry]);
      CIRCLEQ_INIT(&mp->lhqh[entry]);
    }

  /* Initialize max # of pages to cache and number of pages in file */
  mp->maxcache = (pageno_t)maxcache;
  mp->npages   = len / pagesize;

  /* Adjust for when file is not multiple of whole page sizes */
  if (len % pagesize) 
    {
      mp->lastpagesize = len % pagesize; /* odd size of last page */
      (mp->npages)++;
    }
  else 
    mp->lastpagesize = pagesize; 

  /* Set pagesize and file handle */
  mp->pagesize = pagesize;
  mp->fd = fd;

  /* Initialize list hash chain */
  for (pageno = 1; pageno <= mp->npages; ++pageno)
    {
      lhead = &mp->lhqh[HASHKEY(pageno)];
      if ((lp = (L_ELEM *)malloc(sizeof(L_ELEM))) == NULL)
        {
          ret = RET_ERROR;
          goto done;
        }
      lp->pgno   = (pageno_t)pageno;     /* set page number */
      lp->eflags = (u_int8_t)ELEM_SYNC; /* valid page exists on disk */
#ifdef STATISTICS
      lp->elemhit = (u_int32_t)0;
      ++(mp->listalloc);
#endif
#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_open: pageno=%d, lhead=%08x\n",pageno,lhead);
#endif
      CIRCLEQ_INSERT_HEAD(lhead, lp, hl); /* add to list */
    } /* end for pageno */

  /* initialize input/output filters and cookie to NULL */
  mp->pgin     = NULL;
  mp->pgout    = NULL;        
  mp->pgcookie = NULL;        
#ifdef STATISTICS
  mp->listhit    = 0;
  mp->cachehit   = 0;
  mp->cachemiss  = 0;
  mp->pagealloc  = 0;
  mp->pageflush  = 0;
  mp->pageget    = 0;
  mp->pagenew    = 0;
  mp->pageput    = 0;
  mp->pageread   = 0;
  mp->pagewrite  = 0;
#endif

done:
  if(ret == RET_ERROR)
    { /* error cleanup */
      if (mp != NULL)
        free(mp);
      /* free up list elements */
      for (entry = 0; entry < HASHSIZE; ++entry)
        {
          while ((lp = mp->lhqh[entry].cqh_first) != (void *)&mp->lhqh[entry]) 
            {
              CIRCLEQ_REMOVE(&mp->lhqh[entry], mp->lhqh[entry].cqh_first, hl);
              free(lp);
            }
        } /* end for entry */
#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_open: ERROR \n");
#endif      
      mp = NULL; /* return value */
    } /* end error cleanup */
  /* Normal cleanup */
#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_open: mp->pagesize=%lu\n",mp->pagesize);
    (void)fprintf(stderr,"mpool_open: mp->lastpagesize=%lu\n",mp->lastpagesize);
    (void)fprintf(stderr,"mpool_open: mp->maxcache=%u\n",mp->maxcache);
    (void)fprintf(stderr,"mpool_open: mp->npages=%u\n",mp->npages);
#ifdef STATISTICS
    (void)fprintf(stderr,"mpool_open: mp->listalloc=%lu\n",mp->listalloc);
#endif
#endif

  return (mp);
} /* mpool_open () */

/*-----------------------------------------------------------------------------
NAME
   mpool_filter -- Initialize input/output filters.
USAGE
  void mpool_filter(mp, pgin, pgout, pgcookie)
  MPOOL *mp;                                    IN: MPOOL cookie
  void (*pgin) __P((void *, pageno_t, void *));   IN: page in filter 
  void (*pgout) __P((void *, pageno_t, void *));  IN: page out filter 
  void *pgcookie;                               IN: filter cookie 
RETURNS
   Nothing
DESCRIPTION
   Initialize input/output filters for user page processing.

   NOTE: the filters must now handle the case where the page
          is the last page which may not be a full 'pagesize' so
          the filters must check for this.

   COMMENT: We don't use these yet.
---------------------------------------------------------------------------- */
void
mpool_filter(mp, pgin, pgout, pgcookie)
  MPOOL *mp;                                   /* MPOOL cookie */
  void (*pgin) __P((void *, pageno_t, void *));  /* page in filter */
  void (*pgout) __P((void *, pageno_t, void *)); /* page out filter */
  void *pgcookie;                              /* filter cookie */
{
  mp->pgin     = pgin;
  mp->pgout    = pgout;
  mp->pgcookie = pgcookie;
} /* mpool_filter() */

/*-----------------------------------------------------------------------------
NAME
   mpool_new -- get a new page of memory
USAGE
   void *mpool_new(mp, pgnoaddr, pagesize, flags)
   MPOOL *mp;         IN: MPOOL cookie 
   pageno_t *pgnoaddr;  IN/OUT:address of newly created page 
   pageno_t pagesize;   IN:page size for last page
   u_int32_t flags;       IN:MPOOL_EXTEND or 0 
RETURNS
   Returns the new page if succesfula and NULL otherwise
DESCRIPTION
    Get a new page of memory. This is where we get new pages for the file.
    This will only return a full page of memory and 
    if the last page is and odd size the user must keep track
    of this as only 'lastpagesize' bytes will be written out
    and as a result if the user fills the last page and
    'lastpagesize' != 'pagesize' the user will lose data.
    'flags' = 0, increase number of pages by 1 and return
                *pgnoaddr = (npages -1)
    'flags' = MPOOL_EXTEND, set page to *pgnoaddr and
                npages = *pgnoaddr + 1

    All returned pages are pinned.
---------------------------------------------------------------------------- */	
void *
mpool_new(mp, pgnoaddr, pagesize, flags)
  MPOOL     *mp;       /* MPOOL cookie */
  pageno_t    *pgnoaddr; /* address of newly create page */
  pageno_t    pagesize;  /* page size for last page*/
  u_int32_t flags;     /* MPOOL_EXTEND or 0 */
{
  struct _hqh  *head  = NULL; /* head of an entry in hash chain */
  struct _lhqh *lhead = NULL; /* head of an entry in list hash chain */
  BKT          *bp   = NULL;  /* bucket element */
  L_ELEM       *lp   = NULL;
  int          ret = RET_SUCCESS;

  /* check inputs */
  if (mp == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }

  /* page overflow? */
  if (mp->npages == MAX_PAGE_NUMBER) 
    {
      fprintf(stderr, "mpool_new: page allocation overflow.\n");
      abort();
    }
#ifdef STATISTICS
  ++mp->pagenew;
#endif
  /*
   * Get a BKT from the cache.  
   * Assign a new page number based upon 'flags'. If flags 
   * is MPOOL_EXTEND then we want to extend file up to '*pgnoaddr' pages.
   * attach it to the head of the hash chain, the tail of the lru chain,
   * and return.
   */
  if ((bp = mpool_bkt(mp)) == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }

  if (!(flags & MPOOL_EXTEND))
    { /* we increase by one page */
      mp->npages++;                      /* number of pages */
      *pgnoaddr = bp->pgno = mp->npages; /* page number */
    } 
  else 
    { /* we extend to *pgnoaddr pages */
      if (*pgnoaddr > MAX_PAGE_NUMBER) 
        {
          (void)fprintf(stderr, "mpool_new: page allocation overflow.\n");
          abort();
        }
      /* If pagesize is odd size then it is size for last page */
      if (mp->pagesize != pagesize) 
        mp->lastpagesize = pagesize;
      bp->pgno = *pgnoaddr;      /* page number to create */
      mp->npages= *pgnoaddr; /* number of pages */
    }
#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_new: increasing #of pages to=%d\n",mp->npages);
#endif  

  /* Pin the page and insert into head of hash chain 
   * and tail of lru chain */
  bp->flags = MPOOL_PINNED;
  head = &mp->hqh[HASHKEY(bp->pgno)];
  CIRCLEQ_INSERT_HEAD(head, bp, hq);
  CIRCLEQ_INSERT_TAIL(&mp->lqh, bp, q);
  
  /* Check to see if this page has ever been referenced */
  lhead = &mp->lhqh[HASHKEY(bp->pgno)];
  for (lp = lhead->cqh_first; lp != (void *)lhead; lp = lp->hl.cqe_next)
    if (lp->pgno == bp->pgno)
      { /* hit */
#ifdef STATISTICS
  ++mp->listhit;
  ++lp->elemhit;
#endif
        ret = RET_SUCCESS;
        goto done;
      } /* end if lp->pgno */

  /* NO hit, new list element */
  if ((lp = (L_ELEM *)malloc(sizeof(L_ELEM))) == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }
  lp->pgno   = bp->pgno;
  lp->eflags = 0;
#ifdef STATISTICS
  lp->elemhit = 0;
  ++mp->listalloc;
#endif
  CIRCLEQ_INSERT_HEAD(lhead, lp, hl); /* add to list */

#ifdef MPOOL_DEBUG
#ifdef STATISTICS
    (void)fprintf(stderr,"mpool_newn: mp->listalloc=%d\n",mp->listalloc);
#endif
#endif
done:
  if(ret == RET_ERROR)
    { /* error cleanup */
      if(lp != NULL)
        free(lp);

      return NULL;
    }
  /* Normal cleanup */

  return (bp->page);
} /* mpool_new() */

/*-----------------------------------------------------------------------------
NAME
   mpool_get - get a specified page by page number.
USAGE
   void *mpool_get(mp, pgno, flags)
   MPOOL *mp;      IN: MPOOL cookie 
   pageno_t pgno;    IN: page number 
   u_int32_t flags;	   IN: not used? 
RETURNS
   The specifed page if successful and NULL otherwise
DESCRIPTION
    Get a page specified by 'pgno'. If the page is not cached then
    we need to create a new page. All returned pages are pinned.

 COMMENT: Note that since we allow mpool_new() to extend a file, sometimes
          we when we create a new page we read a page from the file
          that was never written previously by the page buffer pool.
          This results in reading garbage into the page.
---------------------------------------------------------------------------- */
void *
mpool_get(mp, pgno, flags)
  MPOOL     *mp;    /* MPOOL cookie */
  pageno_t    pgno;   /* page number */
  u_int32_t flags;  /* XXX not used? */
{
  struct _hqh  *head  = NULL; /* head of lru queue */
  struct _lhqh *lhead = NULL; /* head of an entry in list hash chain */
  BKT          *bp   = NULL;  /* bucket element */
  L_ELEM       *lp   = NULL;
  int          ret   = RET_SUCCESS;
  off_t        off;         /* file offset? */
  pageno_t       nr;          /* number of bytes read for page */
  pageno_t       rpagesize;   /* pagesize to read */
  int          list_hit;    /* hit flag */

#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_get: entering \n");
#endif
  /* check inputs */
  if (mp == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }

  /* Check for attempting to retrieve a non-existent page. 
  *  remember pages go from 1 ->npages  */
  if (pgno > mp->npages) 
    {
      errno = EINVAL;
      ret = RET_ERROR;
      goto done;
    } 

#ifdef STATISTICS
  ++mp->pageget;
#endif

  /* Check for a page that is cached. */
  if ((bp = mpool_look(mp, pgno)) != NULL) 
    {
#ifdef MPOOL_DEBUG
      if (bp->flags & MPOOL_PINNED) 
        {
          (void)fprintf(stderr,
                        "mpool_get: page %d already pinned\n", bp->pgno);
          abort();
        }
#endif
      /*
     * Move the page to the head of the hash chain and the tail
     * of the lru chain.
     */
      head = &mp->hqh[HASHKEY(bp->pgno)];
      CIRCLEQ_REMOVE(head, bp, hq);
      CIRCLEQ_INSERT_HEAD(head, bp, hq);
      CIRCLEQ_REMOVE(&mp->lqh, bp, q);
      CIRCLEQ_INSERT_TAIL(&mp->lqh, bp, q);
      /* Return a pinned page. */
      bp->flags |= MPOOL_PINNED;

#ifdef MPOOL_DEBUG
      (void)fprintf(stderr,"mpool_get: getting cached bp->pgno=%d,npages=%d\n",
                    bp->pgno,mp->npages);
#endif   
      /* update this page reference */
      lhead = &mp->lhqh[HASHKEY(bp->pgno)];
      for (lp = lhead->cqh_first; lp != (void *)lhead; lp = lp->hl.cqe_next)
        if (lp->pgno == bp->pgno)
          { /* hit */
#ifdef STATISTICS
            ++mp->listhit;
            ++lp->elemhit;
#endif
            break;
          } /* end if lp->pgno */

      ret = RET_SUCCESS;
      goto done;
    } /* end if bp */

#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_get: NOT cached page\n");
#endif
  /* Page not cached so
   * Get a page from the cache to use or create one. */
  if ((bp = mpool_bkt(mp)) == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }

  /* Check to see if this page has ever been referenced */
  list_hit = 0;
  lhead = &mp->lhqh[HASHKEY(pgno)];
  for (lp = lhead->cqh_first; lp != (void *)lhead; lp = lp->hl.cqe_next)
    if (lp->pgno == pgno)
      { /* hit */
#ifdef STATISTICS
        ++mp->listhit;
        ++lp->elemhit;
#endif
        list_hit = 1;
        break;
      } /* end if lp->pgno */

  /* If there is no hit then we allocate a new element 
  *  and insert into hash table */
  if (!list_hit)
    { /* NO hit, new list element */
      if ((lp = (L_ELEM *)malloc(sizeof(L_ELEM))) == NULL)
        {
          ret = RET_ERROR;
          goto done;
        }
      lp->pgno = pgno;
      lp->eflags = 0;
#ifdef STATISTICS
      ++mp->listalloc;
      lp->elemhit =1;
#endif
      CIRCLEQ_INSERT_HEAD(lhead, lp, hl); /* add to list */
#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_get: skiping reading in page=%u\n",pgno);
#endif
      goto skip_read;  /* no need to read this page from disk */
    }

  /* Indiate we are reading this page */
  lp->eflags = ELEM_READ; 

#ifdef STATISTICS
  ++mp->pageread;
#endif

  /* Check to see if we are reading in last page */
  if (pgno != mp->npages) 
    { /* regular page */
      rpagesize = mp->pagesize;
#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_get: reading in page=%d\n",pgno);
#endif   
    }
  else 
    { /* reading in last page */
      rpagesize = mp->lastpagesize;
#ifdef MPOOL_DEBUG
      (void)fprintf(stderr,"mpool_get: reading in last page=%d\n",pgno);
#endif  
    }

  /* Get ready to read page */
  off = mp->pagesize * (pgno -1);
  if (FMPI_SEEK(mp->fd, off) == FAIL)
    {
      ret = RET_ERROR;
      goto done;
    }

  /* We do this to see if we really have reached this postion */
  if (FMPI_TELL(mp->fd) != off)
    {
#ifdef MPOOL_DEBUG
      (void)fprintf(stderr,"mpool_get: lseek error=%d\n",off);
#endif
      ret = RET_ERROR;
      goto done;
    }

  /* Read in the contents. */
  if ((nr = FMPI_READ(mp->fd, bp->page, rpagesize)) != rpagesize) 
    {
      if (nr >= 0)
        errno = EFTYPE;
      ret = RET_ERROR;
      goto done;
    }

skip_read:
  /* Set the page number, pin the page. */
  bp->pgno = pgno;
  bp->flags = MPOOL_PINNED;

  /*
   * Add the page to the head of the hash chain and the tail
   * of the lru chain.
   */
  head = &mp->hqh[HASHKEY(bp->pgno)];
  CIRCLEQ_INSERT_HEAD(head, bp, hq);
  CIRCLEQ_INSERT_TAIL(&mp->lqh, bp, q);

  /* Run through the user's filter. */
  if (mp->pgin != NULL)
    (mp->pgin)(mp->pgcookie, bp->pgno, bp->page);

done:
  if(ret == RET_ERROR)
    { /* error cleanup */
#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_get: Error exiting \n");
#endif
      if (lp!=NULL)
        free(lp);
      return NULL;
    }
  /* Normal cleanup */
#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_get: Exiting \n");
#endif
  return (bp->page);
} /* mpool_get() */

/*-----------------------------------------------------------------------------
NAME
   mpool_put -- put a page back into the memory buffer pool
USAGE
   int mpool_put(mp, page, flags)
   MPOOL *mp;     IN: MPOOL cookie 
   void *page;    IN: page to put 
   u_int32_t flags;   IN: flags = 0, MPOOL_DIRTY 
RETURNS
   RET_SUCCESS if succesful and RET_ERROR otherwise
DESCRIPTION
    Return a page to the buffer pool. Unpin it and mark it 
    appropriately i.e. MPOOL_DIRTY
---------------------------------------------------------------------------- */
int
mpool_put(mp, page, flags)
  MPOOL     *mp;    /* MPOOL cookie */
  void      *page;   /* page to put */
  u_int32_t flags;  /* flags = 0, MPOOL_DIRTY */
{
  struct _lhqh *lhead = NULL; /* head of an entry in list hash chain */
  L_ELEM       *lp    = NULL;
  BKT          *bp = NULL;    /* bucket element ptr */
  int          ret = RET_SUCCESS;

  /* check inputs */
  if (mp == NULL || page == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }
#ifdef STATISTICS
  ++mp->pageput;
#endif
  /* get pointer to bucket element */
  bp = (BKT *)((char *)page - sizeof(BKT));
#ifdef MPOOL_DEBUG
  (void)fprintf(stderr,"mpool_put: putting page=%d\n",bp->pgno);
  if (!(bp->flags & MPOOL_PINNED)) 
    {
      (void)fprintf(stderr,
                    "mpool_put: page %d not pinned\n", bp->pgno);
      abort();
    }
#endif
  /* Unpin the page and mark it appropriately */
  bp->flags &= ~MPOOL_PINNED;
  bp->flags |= flags & MPOOL_DIRTY;

  if (bp->flags & MPOOL_DIRTY)
    {
      /* update this page reference */
      lhead = &mp->lhqh[HASHKEY(bp->pgno)];
      for (lp = lhead->cqh_first; lp != (void *)lhead; lp = lp->hl.cqe_next)
        if (lp->pgno == bp->pgno)
          { /* hit */
#ifdef STATISTICS
            ++mp->listhit;
            ++lp->elemhit;
#endif
            lp->eflags = ELEM_WRITTEN;
            break;
          } /* end if lp->pgno */
    }

done:
  if(ret == RET_ERROR)
    { /* error cleanup */
      return RET_ERROR;
    }
  /* Normal cleanup */

  return (RET_SUCCESS);
} /* mpool_put () */

/*-----------------------------------------------------------------------------
NAME
   mpool_close - close the memory buffer pool
USAGE
   int mpool_close(mp)
   MPOOL *mp;  IN: MPOOL cookie 
RETURNS
   RET_SUCCESS if succesful and RET_ERROR otherwise   
DESCRIPTION
   Close the buffer pool.  Frees the buffer pool.
   Does not sync the buffer pool.
---------------------------------------------------------------------------- */
int
mpool_close(mp)
  MPOOL *mp; /* MPOOL cookie */
{
  L_ELEM  *lp = NULL;
  BKT     *bp = NULL;   /* bucket element */
  int     nelem = 0;
  int     ret   = RET_SUCCESS;
  int     entry;      /* index into hash table */

#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_close: entered \n");
#endif
  /* check inputs */
  if (mp == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }

  /* Free up any space allocated to the lru pages. */
  while ((bp = mp->lqh.cqh_first) != (void *)&mp->lqh) 
    {
      CIRCLEQ_REMOVE(&mp->lqh, mp->lqh.cqh_first, q);
      free(bp);
    }

  /* free up list elements */
  for (entry = 0; entry < HASHSIZE; ++entry)
    {
      while ((lp = mp->lhqh[entry].cqh_first) != (void *)&mp->lhqh[entry]) 
        {
          CIRCLEQ_REMOVE(&mp->lhqh[entry], mp->lhqh[entry].cqh_first, hl);
          free(lp);
          nelem++;
        }
    } /* end for entry */

done:
  if(ret == RET_ERROR)
    { /* error cleanup */
      return RET_ERROR;
    }
  /* Normal cleanup */

  /* Free the MPOOL cookie. */
  free(mp);

#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_close: freed %d list elements\n\n",nelem);
#endif
  return (RET_SUCCESS);
} /* mpool_close() */

/*-----------------------------------------------------------------------------
NAME
   mpool_sync -- sync the memory buffer pool
USAGE
   int mpool_sync(mp)
   MPOOL *mp; IN: MPOOL cookie 
RETURNS
   RET_SUCCESS if succesful and RET_ERROR otherwise   
DESCRIPTION
   Sync the pool to disk. Does NOT Free the buffer pool.
---------------------------------------------------------------------------- */
int
mpool_sync(mp)
  MPOOL *mp; /* MPOOL cookie */
{
  BKT *bp = NULL; /* bucket element */
  int ret = RET_SUCCESS;

#ifdef MPOOL_DEBUG
      (void)fprintf(stderr,"mpool_sync: entering \n");
#endif
  /* check inputs */
  if (mp == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }

  /* Walk the lru chain, flushing any dirty pages to disk. */
  for (bp = mp->lqh.cqh_first; bp != (void *)&mp->lqh; bp = bp->q.cqe_next)
    {
      if (bp->flags & MPOOL_DIRTY 
          && mpool_write(mp, bp) == RET_ERROR)
        {
          ret = RET_ERROR;
          goto done;
        }
    } /* end for bp */

done:
  if(ret == RET_ERROR)
    { /* error cleanup */
      return RET_ERROR;
    }
  /* Normal cleanup */

#ifdef MPOOL_DEBUG
      (void)fprintf(stderr,"mpool_sync: exiting \n");
#endif
  /* Sync the file descriptor. This is an expensive operation */
  return (FMPI_FLUSH(mp->fd) ? RET_ERROR : RET_SUCCESS);
} /* mpool_sync() */

/*-----------------------------------------------------------------------------
NAME
   mpool_page_sync -- write the specified page to disk given its page number
USAGE
   int mpool_page_sync(mp, pgno, flags)
   MPOOL *mp;     IN: MPOOL cookie 
   pageno_t pgno;   IN: page number 
   u_int32_t flags;	  IN: not used? 
RETURNS
   RET_SUCCESS if succesful and RET_ERROR otherwise   
DESCRIPTION
   Write a cached page to disk given it's page number
   If the page is not cached return an error.
  
  COMMENT: This is mainly used in the case where we extend the file.
           We need to mark the current file size by writing out
           the last page(or part of it) otherwise mpool_get() on
           an intermediate page between the current end of the file
           and the new end of file will fail.
---------------------------------------------------------------------------- */
int
mpool_page_sync(mp, pgno, flags)
  MPOOL     *mp;     /* MPOOL cookie */
  pageno_t    pgno;    /* page number */
  u_int32_t flags;	  /* XXX not used? */
{
  struct _lhqh *lhead = NULL; /* head of an entry in list hash chain */
  L_ELEM       *lp    = NULL;
  BKT          *bp    = NULL; /* bucket element */
  int          ret = RET_SUCCESS;
  off_t        off;               /* offset into the file */
  pageno_t       wpagesize;         /* page size to write */


#ifdef MPOOL_DEBUG
          (void)fprintf(stderr,"mpool_page_sync: entering\n");
#endif  
  /* check inputs */
  if (mp == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }

  /* Check for attempting to sync a non-existent page. 
  *  remember pages go from 1 ->npages  */
  if (pgno > mp->npages) 
    {
      errno = EINVAL;
      ret = RET_ERROR;
      goto done;
    } 

  /* Check for a page that is cached. */
  if ((bp = mpool_look(mp, pgno)) != NULL) 
    {
#ifdef MPOOL_DEBUG
      if (bp->flags & MPOOL_PINNED) 
        {
          (void)fprintf(stderr,
                        "mpool_page_sync: page %u already pinned\n", bp->pgno);
          abort();
        }
#endif

      /* only flush the page if dirty */
      if (!(bp->flags & MPOOL_DIRTY))
        {
          ret = RET_SUCCESS;
          goto done;
        }

#ifdef STATISTICS
      ++mp->pagewrite;
#endif

      /* update this page reference */
      lhead = &mp->lhqh[HASHKEY(bp->pgno)];
      for (lp = lhead->cqh_first; lp != (void *)lhead; lp = lp->hl.cqe_next)
        if (lp->pgno == bp->pgno)
          { /* hit */
#ifdef STATISTICS
            ++mp->listhit;
            ++lp->elemhit;
#endif
            lp->eflags = ELEM_SYNC;
            break;
          }

      /* Run page through the user's filter. */
      if (mp->pgout)
        (mp->pgout)(mp->pgcookie, bp->pgno, bp->page);

#ifdef MPOOL_DEBUG
      (void)fprintf(stderr,"mpool_page_sync: npages=%u\n",mp->npages);
#endif

      /* Check to see if we are writing last page */
      if (bp->pgno != mp->npages) 
        {
          wpagesize = mp->pagesize;
#ifdef MPOOL_DEBUG
          (void)fprintf(stderr,"mpool_page_sync: writing page=%u\n",bp->pgno);
#endif  
        }
      else 
        { /* writing last page */
          wpagesize = mp->lastpagesize;
#ifdef MPOOL_DEBUG
          (void)fprintf(stderr,"mpool_page_sync: writing last page=%u\n",bp->pgno);
          (void)fprintf(stderr,"mpool_page_sync: lastpagesize=%u\n",mp->lastpagesize);
#endif  

        }

      /* seek to proper postion */
      off = mp->pagesize * (bp->pgno - 1);
      if (FMPI_SEEK(mp->fd, off) == FAIL)
        {
#ifdef MPOOL_DEBUG
          (void)fprintf(stderr,"mpool_page_sync: lseek error=%d\n",off);
#endif
          ret = RET_ERROR;
          goto done;
        }

      /* We do this to see if we really have reached this postion */
      if (FMPI_TELL(mp->fd) != off)
        {
#ifdef MPOOL_DEBUG
          (void)fprintf(stderr,"mpool_page_sync: lseek error=%d\n",off);
#endif
          ret = RET_ERROR;
          goto done;
        }

      /* write page out */
      if (FMPI_WRITE(mp->fd, bp->page, wpagesize) != wpagesize)
        {
#ifdef MPOOL_DEBUG
          perror("mpool_page_sync");
          (void)fprintf(stderr,"mpool_page_sync: fd=%d,lseek =%d, wpagesize=%u\n",
                        mp->fd,off,wpagesize);
          (void)fprintf(stderr,"mpool_page_sync: write error for page=%u\n",bp->pgno);
#endif
          ret = RET_ERROR;
          goto done;
        }

      /* mark page as clean */
      bp->flags &= ~MPOOL_DIRTY;

    } /* end if cached page */
  else /* not a cached page!...we shouldn't encounter this */
    ret = RET_ERROR;

done:
  if(ret == RET_ERROR)
    { /* error cleanup */
      return RET_ERROR;
    }
  /* Normal cleanup */

#ifdef MPOOL_DEBUG
          (void)fprintf(stderr,"mpool_page_sync: exiting\n");
#endif  
  return (RET_SUCCESS);
} /* mpool_page_sync() */

/*-----------------------------------------------------------------------------
NAME
   mpool_bkt - Get a page from the cache (or create one).
USAGE
   static BKT * mpool_bkt(mp)
   MPOOL *mp;  IN: MPOOL cookie 
RETURNS
   A page if successful and NULL otherwise.
DESCRIPTION
  Private routine. Get a page from the cache (or create one).
       
  COMMENT: Note that the size of the page allocated is equal to
           sizeof(bucket element) + pagesize. We only return the
           pagesize fragment to the user. The only caveat here is
           that a user could inadvertently clobber the bucket element
           information by writing out of the page size bounds.
---------------------------------------------------------------------------- */
#ifndef USE_INLINE
static BKT *
#else
BKT *
#endif
mpool_bkt(mp)
  MPOOL *mp;  /* MPOOL cookie */
{
  struct _hqh *head = NULL;  /* head of hash chain */
  BKT         *bp   = NULL;  /* bucket element */
  int          ret  = RET_SUCCESS;

  /* check inputs */
  if (mp == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }

  /* If under the max cached, always create a new page. */
  if ((pageno_t)mp->curcache < (pageno_t)mp->maxcache)
    goto new;

  /*
   * If the cache is max'd out, walk the lru list for a buffer we
   * can flush.  If we find one, write it (if necessary) and take it
   * off any lists.  If we don't find anything we grow the cache anyway.
   * The cache never shrinks.
   */
  for (bp = mp->lqh.cqh_first; bp != (void *)&mp->lqh; bp = bp->q.cqe_next)
    if (!(bp->flags & MPOOL_PINNED)) 
      { /* Flush if dirty. */
        if (bp->flags & MPOOL_DIRTY  && mpool_write(mp, bp) == RET_ERROR)
          {
            ret = RET_ERROR;
            goto done;
          }
#ifdef STATISTICS
        ++mp->pageflush;
#endif
        /* Remove from the hash and lru queues. */
        head = &mp->hqh[HASHKEY(bp->pgno)];
        CIRCLEQ_REMOVE(head, bp, hq);
        CIRCLEQ_REMOVE(&mp->lqh, bp, q);
#ifdef MPOOL_DEBUG
        { void *spage;
        spage = bp->page;
        memset(bp, 0xff, sizeof(BKT) + mp->pagesize);
        bp->page = spage;
        }
#endif
        ret = RET_SUCCESS;
        goto done;
      } /* end if bp->flags */

  /* create a new page */
new: if ((bp = (BKT *)malloc(sizeof(BKT) + mp->pagesize)) == NULL)
          {
            ret = RET_ERROR;
            goto done;
          }

#ifdef STATISTICS
  ++mp->pagealloc;
#endif

#if defined(MPOOL_DEBUG) || defined(PURIFY)
  memset(bp, 0xff, sizeof(BKT) + mp->pagesize);
#endif

  /* set page ptr past bucket element section */
  bp->page = (char *)bp + sizeof(BKT);
  ++mp->curcache; /* increase number of cached pages */

done:
  if(ret == RET_ERROR)
    { /* error cleanup */
      if (bp != NULL)
        free(bp);

      return NULL;
    }
  /* Normal cleanup */

  return (bp); /* return only the pagesize fragement */
} /* mpool_bkt() */

/*-----------------------------------------------------------------------------
NAME
   mpool_write - write a page to disk given it's bucket handle.
USAGE
   static int mpool_write(mp, bp)
   MPOOL *mp;     IN: MPOOL cookie 
   BKT *bp;       IN: bucket element 
RETURNS
   RET_SUCCESS if succesful and RET_ERROR otherwise   
DESCRIPTION
   Private routine. Write a page to disk given it's bucket handle.
---------------------------------------------------------------------------- */
static int
mpool_write(mp, bp)
  MPOOL *mp;     /* MPOOL cookie */
  BKT *bp;       /* bucket element */
{
  struct _lhqh *lhead = NULL; /* head of an entry in list hash chain */
  L_ELEM       *lp   = NULL;
  int          ret = RET_SUCCESS;
  off_t        off;      /* offset into the file */
  pageno_t       wpagesize;  /* page size to write */


#ifdef MPOOL_DEBUG
      (void)fprintf(stderr,"mpool_write: entering \n");
#endif
  /* check inputs */
  if (mp == NULL || bp == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }

#ifdef STATISTICS
  ++mp->pagewrite;
#endif

  /* update this page reference */
  lhead = &mp->lhqh[HASHKEY(bp->pgno)];
  for (lp = lhead->cqh_first; lp != (void *)lhead; lp = lp->hl.cqe_next)
    if (lp->pgno == bp->pgno)
      { /* hit */
#ifdef STATISTICS
        ++mp->listhit;
        ++lp->elemhit;
#endif
        lp->eflags = ELEM_SYNC;
        break;
      }

  /* Run page through the user's filter. */
  if (mp->pgout)
    (mp->pgout)(mp->pgcookie, bp->pgno, bp->page);

#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_write: npages=%u\n",mp->npages);
#endif

  /* Check to see if we are writing last page */
  if (bp->pgno != mp->npages) 
    {
      wpagesize = mp->pagesize;
#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_write: writing page=%u\n",bp->pgno);
#endif  
    }
  else 
    { /* writing last page */
      wpagesize = mp->lastpagesize;
#ifdef MPOOL_DEBUG
    (void)fprintf(stderr,"mpool_write: writing last page=%u\n",bp->pgno);
    (void)fprintf(stderr,"mpool_write: lastpagesize=%u\n",mp->lastpagesize);
#endif  

    }

  /* seek to proper postion */
  off = mp->pagesize * (bp->pgno - 1);
  if (FMPI_SEEK(mp->fd, off) == FAIL)
    {
#ifdef MPOOL_DEBUG
      (void)fprintf(stderr,"mpool_write: lseek error=%d\n",off);
#endif
      ret = RET_ERROR;
      goto done;
    }

  /* We do this to see if we really have reached this postion */
  if (FMPI_TELL(mp->fd) != off)
    {
#ifdef MPOOL_DEBUG
      (void)fprintf(stderr,"mpool_page_sync: lseek error=%d\n",off);
#endif
      ret = RET_ERROR;
      goto done;
    }

  /* write page out */
  if (FMPI_WRITE(mp->fd, bp->page, wpagesize) != wpagesize)
    {
#ifdef MPOOL_DEBUG
      perror("mpool_write");
      (void)fprintf(stderr,"mpool_write: write error\n");
#endif
      ret = RET_ERROR;
      goto done;
    }

  /* mark page as clean */
  bp->flags &= ~MPOOL_DIRTY;

done:
  if(ret == RET_ERROR)
    { /* error cleanup */
#ifdef MPOOL_DEBUG
      (void)fprintf(stderr,"mpool_write: error exiting\n");
#endif
      return RET_SUCCESS;
    }
  /* Normal cleanup */

#ifdef MPOOL_DEBUG
      (void)fprintf(stderr,"mpool_write: exiting\n");
#endif
  return (RET_SUCCESS);
} /* mpool_write() */

/*-----------------------------------------------------------------------------
NAME
   mpool_look - lookup a page in the cache.
USAGE
   static BKT *mpool_look(mp, pgno)
   MPOOL *mp;    IN: MPOOL cookie 
   pageno_t pgno;  IN: page to look up in cache 
RETURNS
   Page if successfull and NULL othewise.
DESCRIPTION
   Private routine. Lookup a page in the cache and return pointer to it.
---------------------------------------------------------------------------- */
#ifndef USE_INLINE
static BKT *
#else
BKT *
#endif
mpool_look(mp, pgno)
  MPOOL *mp;    /* MPOOL cookie */
  pageno_t pgno;  /* page to look up in cache */
{
  struct _hqh *head = NULL; /* head of hash chain */
  BKT         *bp   = NULL; /* bucket element */
  int          ret  = RET_SUCCESS;

  /* check inputs */
  if (mp == NULL)
    {
      ret = RET_ERROR;
      goto done;
    }

  /* Check for attempt to look up a non-existent page. */
  if (pgno > mp->npages) 
    {
      errno = EINVAL;
      ret = RET_ERROR;
      goto done;
    }

  /* search through hash chain */
  head = &mp->hqh[HASHKEY(pgno)];
  for (bp = head->cqh_first; bp != (void *)head; bp = bp->hq.cqe_next)
    if (bp->pgno == pgno) 
      { /* hit....found page in cache */
#ifdef STATISTICS
        ++mp->cachehit;
#endif
        ret = RET_SUCCESS;
        goto done;
      }

  /* Well didn't find page in cache so mark return
  * value as NULL */
  bp = NULL; 

#ifdef STATISTICS
  ++mp->cachemiss;
#endif
done:
  if(ret == RET_ERROR)
    { /* error cleanup */
      return NULL;
    }
  /* Normal cleanup */

  return (bp);
} /* mpool_look() */

#ifdef STATISTICS
#ifdef HAVE_GETRUSAGE
/*-----------------------------------------------------------------------------
NAME
   myrusage - print some process usage statistics
USAGE
   void myrusage()
RETURNS
   Nothing
DESCRIPTION
   prints some process usage statistics to STDERR
---------------------------------------------------------------------------- */
void
myrusage()
{
    struct rusage r;
    double sys, user, idle;
    double per;
    double timespent();

    getrusage(RUSAGE_SELF,&r);
    fprintf(stderr,"USAGE: shmem=%d,unshdata=%d,unshstack=%d\n",
            r.ru_ixrss,r.ru_idrss,r.ru_isrss);
    fprintf(stderr,"       pager=%d,pagef=%d,nswap=%d\n",
            r.ru_minflt,r.ru_majflt,r.ru_nswap);
    fprintf(stderr,"       block_in=%d,block_out=%d,nioch=%d\n",
            r.ru_inblock,r.ru_oublock,r.ru_ioch);
    fprintf(stderr,"       mesgs=%d,mesgr=%d,nsignals=%d\n",
            r.ru_msgsnd,r.ru_msgrcv,r.ru_nsignals);
}
#endif /* HAVE_GETRUSAGE */

/*-----------------------------------------------------------------------------
NAME
   mpool_stat - print out cache statistics
USAGE
   void mpool_stat(mp)
   MPOOL *mp; IN: MPOOL cookie 
RETURNS
   Nothing
DESCRIPTION
   Print out cache statistics to STDERR.
---------------------------------------------------------------------------- */
void
mpool_stat(mp)
  MPOOL *mp; /* MPOOL cookie */
{
  struct _lhqh *lhead = NULL; /* head of an entry in list hash chain */
  BKT          *bp    = NULL; /* bucket element */
  L_ELEM       *lp    = NULL;
  char         *sep   = NULL;
  int          entry;         /* index into hash table */
  int          cnt;
  int          hitcnt; 

#ifdef HAVE_GETRUSAGE
  myrusage();
#endif

  /* check inputs */
  if (mp != NULL)
    {
      (void)fprintf(stderr, "%u pages in the file\n", mp->npages);
      (void)fprintf(stderr,
                    "page size %u, cacheing %u pages of %u page max cache\n",
                    mp->pagesize, mp->curcache, mp->maxcache);
      (void)fprintf(stderr, "%u page puts, %u page gets, %u page new\n",
                    mp->pageput, mp->pageget, mp->pagenew);
      (void)fprintf(stderr, "%u page allocs, %u page flushes\n",
                    mp->pagealloc, mp->pageflush);
      if (mp->cachehit + mp->cachemiss)
        (void)fprintf(stderr,
                      "%.0f%% cache hit rate (%u hits, %u misses)\n", 
                      ((double)mp->cachehit / (mp->cachehit + mp->cachemiss))
                      * 100, mp->cachehit, mp->cachemiss);
      (void)fprintf(stderr, "%u page reads, %u page writes\n",
                    mp->pageread, mp->pagewrite);
      (void)fprintf(stderr, "%u listhits, %u listallocs\n",
                    mp->listhit, mp->listalloc);
     (void)fprintf(stderr, "sizeof(MPOOL)=%d, sizeof(BKT)=%d, sizeof(L_ELEM)=%d\n",
                    sizeof(MPOOL), sizeof(BKT), sizeof(L_ELEM));
      (void)fprintf(stderr, "memory pool used %u bytes\n",
              (u_int32_t)(sizeof(MPOOL)+ (sizeof(BKT)+mp->pagesize)*mp->curcache +
                (sizeof(L_ELEM)*mp->npages)));
      sep = "";
      cnt = 0;
      for (bp = mp->lqh.cqh_first; bp != (void *)&mp->lqh; bp = bp->q.cqe_next) 
        {
          (void)fprintf(stderr, "%s%u", sep, bp->pgno);
          if (bp->flags & MPOOL_DIRTY)
            (void)fprintf(stderr, "d");
          if (bp->flags & MPOOL_PINNED)
            (void)fprintf(stderr, "P");
          if (++cnt == 10) 
            {
              sep = "\n";
              cnt = 0;
            } 
          else
            sep = ", ";
        }
      (void)fprintf(stderr, "\n");
      (void)fprintf(stderr, "Element hits\n");
      sep = "";
      cnt = 0;
      hitcnt = 0;
      for (entry = 0; entry < HASHSIZE; ++entry)
        {
          lhead = &mp->lhqh[entry];
          for (lp = lhead->cqh_first; lp != (void *)lhead; lp = lp->hl.cqe_next)
            {
              cnt++;
              (void)fprintf(stderr, "%s%u(%u)", sep, lp->pgno, lp->elemhit);
              hitcnt += lp->elemhit;
              if (cnt >= 8) 
                {
                  sep = "\n";
                  cnt = 0;
                } 
              else
                sep = ", ";
            }
          if (cnt >= 8) 
            {
              (void)fprintf(stderr, "\n");
              cnt = 0;
            } 
        }
      (void)fprintf(stderr, "\n");
      (void)fprintf(stderr, "Total num of elemhits=%d\n",hitcnt);
    } /* end if mp */
}
#endif /* STATISTICS */
