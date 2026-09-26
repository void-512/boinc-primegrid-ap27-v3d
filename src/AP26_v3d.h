// Keep BOINC and result handling in AP26_v3d.cpp. A K is committed only after
// every shift and tile succeeds, so an interrupted K can safely restart.
void SearchAP26(int K, int startSHIFT)
{
	uint64_t n0=(N0*(K%17835)+((N0*17835)%MOD)*(K/17835)+N30)%MOD;
	uint64_t S31=(PRES2*(K%17835)+((PRES2*17835)%MOD)*(K/17835))%MOD;
	uint64_t S37=(PRES3*(K%17835)+((PRES3*17835)%MOD)*(K/17835))%MOD;
	uint64_t S41=(PRES4*(K%17835)+((PRES4*17835)%MOD)*(K/17835))%MOD;
	int count=0;
	for(int i31=0;i31<7;i31++) for(int i37=0;i37<13;i37++)
	if(i37-i31<=10 && i31-i37<=4)
	for(int i41=0;i41<17;i41++)
	if(i41-i31<=14 && i41-i37<=14 && i31-i41<=4 && i37-i41<=10)
	for(int i3=0;i3<2;i3++) for(int i5=0;i5<4;i5++)
		n43_h[count++]=(n0+i3*S3+i5*S5+i31*S31+i37*S37+i41*S41)%MOD;
	if(count!=numn43s) { fprintf(stderr,"n43 count mismatch: %d\n",count); exit(EXIT_FAILURE); }
	try {
		auto hits=search_ap27_k((unsigned)K,(unsigned)startSHIFT,n43_h,[&](double fraction){
			Progress((K_DONE+fraction)/K_COUNT);
			checkpoint(startSHIFT,K,0);
		});
		for(const auto& h:hits) ReportSolution(h.length,K,h.first);
		totalaps+=(uint32_t)hits.size();
	} catch(const std::exception& e) {
		fprintf(stderr,"AP27 search failed for K=%d: %s\n",K,e.what()); exit(EXIT_FAILURE);
	}
}
