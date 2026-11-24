main(){level.callbackStartGameType=::StartTest;}
StartTest(){setcvar("gsc_test_starts",getcvarint("gsc_test_starts")+1);
 e=getent("startup_panel","targetname");e hide();setcvar("gsc_test_phase","hidden");
 wait 2;e show();setcvar("gsc_test_phase","shown");
 setcvar("gsc_test_completions",getcvarint("gsc_test_completions")+1);
 wait 0.1;setcvar("gsc_test_tails",getcvarint("gsc_test_tails")+1);}
