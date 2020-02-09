#include <unistd.h>
#include <Security/Security.h>

#include "machmemory.h"

//
// /usr/include/mach/mach_traps.h
// extern kern_return_t task_for_pid( mach_port_name_t target_tport, int pid, mach_port_name_t *t);
//
// /usr/include/mach/mach_init.h
// mach_port_t mach_task_self();
//
// /usr/include/mach/vm_map.h
// extern kern_return_t vm_read( vm_map_t target_task, vm_address_t address, vm_size_t size, vm_offset_t *data, mach_msg_type_number_t *dataCnt);
//
// http://www.linuxselfhelp.com/gnu/machinfo/html_chapter/mach_5.html
// see ~/sources/osx/gdb-1344/src/gdb/gdbserver/macosx-mutils.c


int macosx_get_task_for_pid_rights(void)
{
  OSStatus stat;
  AuthorizationItem taskport_item[] = {{"system.privilege.taskport.debug"}};
  AuthorizationRights rights = {1, taskport_item}, *out_rights = NULL;
  AuthorizationRef author;
  int retval = 0;

  AuthorizationFlags auth_flags = kAuthorizationFlagExtendRights
    | kAuthorizationFlagPreAuthorize
    | kAuthorizationFlagInteractionAllowed
    | ( 1 << 5) /* kAuthorizationFlagLeastPrivileged */;
 
  stat = AuthorizationCreate (NULL, kAuthorizationEmptyEnvironment, 
                              auth_flags,
                              &author);
  if (stat != errAuthorizationSuccess)
    return 0;

  /* If you have a window server connection, then this call will put
     up a dialog box if it can.  However, if the current user doesn't
     have a connection to the window server (for instance if they are
     in an ssh session) then this call will return
     errAuthorizationInteractionNotAllowed.  
     I want to do this way first, however, since I'd prefer the dialog
     box - for instance if I'm running under Xcode - to trying to prompt.  */

  stat = AuthorizationCopyRights (author, &rights, kAuthorizationEmptyEnvironment,
                                  auth_flags,
                                  &out_rights);
  if (stat == errAuthorizationSuccess)
    {
      retval = 1;
      goto cleanup;
    }
  else if (stat == errAuthorizationInteractionNotAllowed)
    {
      /* Okay, so the straight call couldn't query, so we're going to
         have to get the username & password and send them by hand to
         AuthorizationCopyRights.  */
      char *pass;
      char *login_name;
      char entered_login[256];
      
      login_name = getlogin ();
      if (! login_name )
        return 0;

      printf("We need authorization from an admin user to run the debugger.\n");
      printf("This will only happen once per login session.\n");
      printf("Admin username (%s): ", login_name);
      fgets (entered_login, 255, stdin);
      if (entered_login[0] != '\n')
        {
          entered_login[strlen (entered_login) - 1] = '\0';
          login_name = entered_login;
        }
      pass = getpass ("Password:");
      if (!pass)
        return 0;

      AuthorizationItem auth_items[] = {
        { kAuthorizationEnvironmentUsername },
        { kAuthorizationEnvironmentPassword },
        { kAuthorizationEnvironmentShared }
      };
      AuthorizationEnvironment env = { 3, auth_items };

      auth_items[0].valueLength = strlen (login_name);
      auth_items[0].value = login_name;
      auth_items[1].valueLength = strlen (pass);
      auth_items[1].value = pass;

      /* If we got rights in the AuthorizationCopyRights call above,
         free it before we reuse the pointer. */
      if (out_rights != NULL)
        AuthorizationFreeItemSet (out_rights);
        
      stat = AuthorizationCopyRights (author, &rights, &env, auth_flags, &out_rights);

      bzero (pass, strlen (pass));
      if (stat == errAuthorizationSuccess)
        retval = 1;
      else
        retval = 0;
    }

 cleanup:
  if (out_rights != NULL)
    AuthorizationFreeItemSet (out_rights);
  AuthorizationFree (author, kAuthorizationFlagDefaults);

  return retval;
}

task_t MachOpenProcessByPid(int pid)
{
    task_t task;
    kern_return_t kret = task_for_pid(mach_task_self(), pid, &task);
    if (kret != KERN_SUCCESS)
    {
        if (macosx_get_task_for_pid_rights() == 1)
            kret = task_for_pid(mach_task_self(), pid, &task);
    }
    if (kret != KERN_SUCCESS)
        throw mach_exception(kret, "task_for_pid");

    return task;
}

