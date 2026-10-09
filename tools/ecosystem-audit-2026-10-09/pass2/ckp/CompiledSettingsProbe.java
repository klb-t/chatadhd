import com.example.core.config.*;
import org.json.JSONObject;
import java.lang.reflect.*;
import java.util.*;

/** Actual Android-build classes; no Settings/Schema/Profile replacement. No Context init. */
public class CompiledSettingsProbe {
 static int failed=0;
 static Object call(Object target,String prefix,Object...args)throws Exception {
  for(Method m:target.getClass().getMethods()) if((m.getName().equals(prefix)||m.getName().startsWith(prefix+"-")||m.getName().startsWith(prefix+"$"))&&m.getParameterCount()==args.length)return m.invoke(target,args);
  throw new NoSuchMethodException(prefix);
 }
 static boolean failure(Object result){return result!=null&&result.getClass().getName().equals("kotlin.Result$Failure");}
 static void result(String id,String lane,boolean pass){if(!pass)failed++;System.out.println(new JSONObject().put("id",id).put("lane",lane).put("status",pass?"PASS":"FAIL"));}
 public static void main(String[]args)throws Exception{
  System.setSecurityManager(new SecurityManager(){public void checkPermission(java.security.Permission p){};public void checkConnect(String h,int p){throw new SecurityException("audit external transport blocked");}});
  Settings base=new Settings();SettingsStore store=SettingsStore.INSTANCE;SettingsProfiles profiles=SettingsProfiles.INSTANCE;
  Object unknown=call(store,"setByKey","audit_unknown_key",true);
  result("A2-CKP-SETTING-UNKNOWN.A","reproduction",!failure(unknown)&&store.getCurrent().equals(base));
  result("A2-CKP-SETTING-UNKNOWN.B","acceptance",failure(unknown));
  result("A2-CKP-PROFILE-UNKNOWN.B","acceptance",failure(call(profiles,"parseImport","{\"audit_unknown_key\":true}")));
  SettingsProfile p=new SettingsProfile("audit-id","audit","",new JSONObject().put("clipboardEnabled",false),false);
  JSONObject encoded=profiles.encode(p);Object decoded=call(profiles,"parseImport",encoded.toString());
  Settings applied=(Settings)call(profiles,"apply",base,decoded);
  result("A2-CKP-PROFILE-CODEC.B","acceptance",!applied.getClipboardEnabled());
  for(SettingsLevel level:new SettingsLevel[]{SettingsLevel.BASIC,SettingsLevel.ADVANCED,SettingsLevel.EXPERT}) {
   Settings selected=SettingsHierarchy.INSTANCE.selectLevel(applied,level);
   result("A2-CKP-SAME-STATE-"+level.name()+".B","acceptance",!selected.getClipboardEnabled());
  }
  JSONObject unsupported=profiles.encode(p).put("version",999);
  result("A2-CKP-PROFILE-VERSION.B","acceptance",failure(call(profiles,"parseImport",unsupported.toString())));
  // Exact persistent codec, separate from profile patch codec. Future field handling is observed.
  JSONObject state=(JSONObject)call(store,"toJson",applied);state.put("audit_future_metadata","fixture");
  Settings restored=(Settings)call(store,"fromJson",state);
  JSONObject again=(JSONObject)call(store,"toJson",restored);
  result("A2-CKP-STATE-UNKNOWN.A","reproduction",!again.has("audit_future_metadata"));
  result("A2-CKP-STATE-DISABLED.B","acceptance",!restored.getClipboardEnabled());
  System.exit(failed==0?0:1);
 }
}
