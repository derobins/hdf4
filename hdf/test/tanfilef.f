C****************************************************************************
C* NCSA HDF                                                                 *
C* Software Development Group                                               *
C* National Center for Supercomputing Applications                          *
C* University of Illinois at Urbana-Champaign                               *
C* 605 E. Springfield, Champaign IL 61820                                   *
C*                                                                          *
C* For conditions of distribution and use, see the accompanying             *
C* hdf/COPYING file.                                                        *
C*                                                                          *
C****************************************************************************
C
C $Id$
C
      subroutine tanfilef (num_err)
C
C
C  Test program: 
C                Writes file labels and descriptions in a file.
C                Reads the  labels and descriptions from the file
C
C  Input file:  none
C  Output files: tdfanflF.hdf
C
C  Possible bug:  When reading in a label, we have to give it a 
C                 length that is one greater than MAXLEN_LAB. This
C                 may be due to a bug in dfan.c in DFANIgetann().
C

      integer daafid, daafds,dagfidl,dagfid
      integer dagfdsl, dagfds, hopen, hclose
      integer ret, number_failed
      integer ISFIRST, NOFIRST, MAXLEN_LAB, MAXLEN_DESC
      integer fid, DFACC_CREATE, DFACC_READ

      character*35 lab1, lab2
      character*35 templab
      character*100 desc1, desc2, tempstr
      character*64 TESTFILE
      character*1 CR

      ISFIRST = 1
      NOFIRST = 0
      number_failed = 0
      TESTFILE = 'tdfanflF.hdf'
      CR = char(10)
      MAXLEN_LAB = 35
      MAXLEN_DESC = 100
      DFACC_CREATE = 4
      DFACC_READ = 1

      lab1 = 'File label #1: aaa'
      lab2 = 'File label #2: bbbbbb'
      desc1 = 'File descr #1: This is a test file annotation'
      desc2 = 'File descr #2: One more test ...'

      print *, '****** Write file labels *******'
      fid = hopen(TESTFILE, DFACC_CREATE, 0)
      call RESULT(fid, 'hopen', number_failed)
      ret = daafid(fid, lab1)
      call RESULT(ret, 'daafid', number_failed)

      ret = daafid(fid, lab2)
      call RESULT(ret, 'daafid', number_failed)

      print *, '****** Write file descriptions *******'
      ret = daafds(fid, desc1, len(desc1))
      call RESULT(ret, 'daafds', number_failed)

      ret = daafds(fid, desc2, len(desc2))
      call RESULT(ret, 'daafds', number_failed)

      ret = hclose(fid)
      call RESULT(ret, 'hclose', number_failed)

      print *, '****** Read length of the first file label ****'
      fid = hopen(TESTFILE, DFACC_READ, 0)
      call RESULT(fid, 'hopen-read', number_failed)
      ret = dagfidl(fid, ISFIRST)
      call RESULT(ret, 'dagfidl', number_failed)
      call checklen(ret, lab1,  'label'  )

      print *, '******...followed by the label *****'
      ret = dagfid(fid, templab, MAXLEN_LAB, ISFIRST)

      call RESULT(ret, 'dagfid', number_failed)
      call checklab(lab1, templab, ret, 'label')

      print *, '****** Read length of the second file label ****'
      ret = dagfidl(fid, NOFIRST)
      call RESULT(ret, 'dagfidl', number_failed)
      call checklen(ret, lab2, 'label')

      print *, '******...followed by the label *****'
      ret = dagfid(fid, templab, MAXLEN_LAB, NOFIRST)
      call RESULT(ret, 'dagfid', number_failed)
      call checklab(lab2, templab, ret, 'label')

      print *, '****** Read length of the first file description ****'
      ret = dagfdsl(fid, ISFIRST)
      call RESULT(ret, 'dagfdsl', number_failed)
      call checklen(ret, desc1, 'description' )

      print *, '******...followed by the description *****'
      ret = dagfds(fid, tempstr, MAXLEN_DESC, ISFIRST)
      call RESULT(ret, 'dagfds', number_failed)
      call checkann(desc1, tempstr, ret, 'description')

      print *, '****** Read length of the second file description ****'
      ret = dagfdsl(fid, NOFIRST)
      call RESULT(ret, 'dagfdsl', number_failed)
      call checklen(ret, desc2, 'description' )

      print *, '******...followed by the description *****'
      ret = dagfds(fid, tempstr, MAXLEN_DESC, NOFIRST)
      call RESULT(ret, 'dagfds', number_failed)
      call checkann(desc2, tempstr, ret, 'description')
     
      ret = hclose(fid)
      call RESULT(ret, 'hclose', number_failed)
 
      print *, CR, CR

      if (number_failed .eq. 0) then
         print *, '***** ALL TESTS SUCCESSFUL ******'
      else
         print *, '********', number_failed, ' TESTS FAILED'
      endif


      return
      end


C*********************************************
C
C  checklen
C
C*********************************************

      subroutine checklen(ret, oldstr, type)
      character*(*) type, oldstr 
      integer ret

      integer oldlen

      oldlen = len(oldstr) 
      if (ret .ge. 0 .and.  ret .ne. oldlen) then
          print *, 'Length of ', type, ' is ', len(oldstr),
     *             ' instead of ', ret
      endif
      return
      end

C***********************************************
C
C  checkann
C
C***********************************************

      subroutine checkann(oldstr, newstr, ret, type)
      character*90  oldstr, newstr
      character*(*) type
      integer ret


      if (ret .ge. 0 .and. oldstr .ne. newstr) then
          print *, type, ' is incorrect.'
          print *, ' It should be <', oldstr, '>'
          print *, ' instead of   <', newstr, '>'
      endif
      return 
      end

C***********************************************
C
C  checklab
C
C***********************************************

      subroutine checklab(oldstr, newstr, ret, type)
      character*30  oldstr, newstr
      character*(*) type
      integer ret


      if (ret .ge. 0 .and. oldstr .ne. newstr) then
          print *, type, ' is incorrect.'
          print *, ' It should be <', oldstr, '>'
          print *, ' instead of   <', newstr, '>'
      endif
      return 
      end
