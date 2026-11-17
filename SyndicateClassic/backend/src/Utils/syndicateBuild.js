// Detect the client build from its own request, never from another player's session.
export function clientBuild(userAgent='') {
 const ua=String(userAgent);
 if(/Release-2\.5(?:-|\s|$)/i.test(ua) || /(?:CL-?|-)(?:3889387)(?:\D|$)/i.test(ua))return '2.5';
 if(/(?:Release-Cert-CL-|CL-?)3807424(?:\D|$)/i.test(ua) || /-3807424\+{3}Fortnite\+Release-Cert(?:\W|$)/i.test(ua))return '1.11';
 if(/Release-1\.11(?:-|\s|$)/i.test(ua))return '1.11';
 if(/Release-4\.(?:5|50)(?:-|\s|$)/i.test(ua))return '4.5';
 // Pre-season 1.7.2 (CL 3700114).
 if(/(?:Release-(?:Live|Cert)-CL-|CL-?)3700114(?:\D|$)/i.test(ua)||/-3700114\+{3}Fortnite\+Release-(?:Live|Cert)(?:\W|$)/i.test(ua))return '1.7.2';
 if(/(?:Release-Live-CL-|CL-?)3724489(?:\D|$)/i.test(ua)||/-3724489\+{3}Fortnite\+Release-Live(?:\W|$)/i.test(ua))return '1.8';
 if(/(?:Release-(?:Live|Cert)-CL-|CL-?)3741772(?:\D|$)/i.test(ua)||/-3741772\+{3}Fortnite\+Release-(?:Live|Cert)(?:\W|$)/i.test(ua))return '1.8.2';
 if(/Release-3\.5-CL-4000805(?:\D|$)/i.test(ua)||/-4000805\+{3}Fortnite\+Release-3\.5(?:\W|$)/i.test(ua))return '3.5.1';
 // Seasons 6-10 (6.20, 7.40, 8.50, 9.40, 10.31, 10.40) identify themselves by their release name.
 const later=/Release-(6\.20|7\.40|8\.50|9\.40|10\.31|10\.40|11\.00|19\.00)(?:-|\s|$)/i.exec(ua); // 11.00: Chapter 2 Season 1 (Dev Kit). 19.00: Chapter 3 Season 1 (Dev Kit, experimental)
 if(later)return later[1];
 // Chapter 2 Season 2: any 12.xx point release (12.00..12.61) is treated as the one 12.50 slot.
 if(/Release-12\.\d+(?:-|\s|$)/i.test(ua))return '12.50';
 const extra=/Release-(1\.7\.2|1\.8\.2|1\.8|3\.5\.1|5\.10)(?:-|\s|$)/i.exec(ua);
 return extra?extra[1]:null;
}

// Both supported Season 2 clients share the legacy account/shop projection.
export const isSeasonTwoBuild = build => build === '1.11' || build === '2.5';
