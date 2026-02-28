#version 330 core

// Data from vertex shader.
// --------------------------------------------------------
// Add your data for interpolation.
// --------------------------------------------------------
in vec3 iPosWorld;//FragPos
in vec3 iNormalWorld;
in vec2 iTexCoord;

// --------------------------------------------------------
// Add your uniform variables.
// --------------------------------------------------------
uniform sampler2D mapKd;
uniform vec3 ambientLight;
uniform vec3 Ka;
uniform vec3 Kd;
uniform vec3 Ks;
uniform float Ns;
uniform vec3 cameraPos;
uniform vec3 dirLightDir;
uniform vec3 dirLightRadiance;
uniform vec3 pointLightPos;
uniform vec3 pointLightIntensity;
uniform vec3 spotLightPosition;
uniform vec3 spotLightDirection;
uniform vec3 spotLightIntensity;
uniform float constant;
uniform float linear;
uniform float quadratic;
uniform float spotLightCutoffStartInDegree;
uniform float spotLightTotalWidthInDegree;


out vec4 FragColor;


void main()
{
    // --------------------------------------------------------
    // Add your implementation.
    // -------------------------------------------------------

    FragColor = texture2D(mapKd,vec2(iTexCoord.x,iTexCoord.y));
    
    //ambient light
    vec3 ambient = ambientLight * Ka;
    vec3 visColor = ambient;
    //FragColor += vec4(visColor, 1.0f);

    //point light
    // Ambient
  	
    // Diffuse 
    vec3 norm = normalize(iNormalWorld);
    vec3 lightDir = normalize(pointLightPos - iPosWorld);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = Kd * diff * pointLightIntensity;  
    
    // Specular
    vec3 viewDir = normalize(cameraPos - iPosWorld);
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), Ns);
    vec3 specular = Ks * spec * pointLightIntensity;
    
    // Attenuation
    float distance    = length(pointLightPos - iPosWorld);
    float attenuation = 1.0f / (distance * distance);    

    ambient  *= attenuation;  
    diffuse  *= attenuation;
    specular *= attenuation;   
            
    FragColor += vec4( diffuse + specular, 1.0f);  

    //directional light
    // Ambient
  	
    // Diffuse 
    norm = normalize(iNormalWorld);
    lightDir = normalize(-dirLightDir);  
    diff = max(dot(norm, lightDir), 0.0);
    diffuse = Kd * diff * dirLightRadiance;  
    
    // Specular
    viewDir = normalize(cameraPos - iPosWorld);
    reflectDir = reflect(-lightDir, norm);  
    spec = pow(max(dot(viewDir, reflectDir), 0.0), Ns);
    specular =Ks * spec * dirLightRadiance;
            
    FragColor += vec4( diffuse + specular, 1.0f)/2; 


    //Spotlight
    vec3 spot_lightDir = normalize(spotLightPosition - iPosWorld);
    
    // Check if lighting is inside the spotlight cone
    float theta = dot(spot_lightDir, normalize(-spotLightDirection)); 
    float epsilon = spotLightCutoffStartInDegree - spotLightTotalWidthInDegree;
    float spot_intensity = clamp((theta - spotLightTotalWidthInDegree) / epsilon, 0.0, 1.0);


    vec3 spot_norm = normalize(iNormalWorld);        
    float spot_diff = max(dot(spot_norm, spot_lightDir), 0.0);
    vec3 spot_diffuse = Kd * spot_diff * spotLightIntensity;  
        
    // Specular
    vec3 spot_viewDir = normalize(cameraPos - iPosWorld);
    vec3 spot_reflectDir = reflect(-spot_lightDir, spot_norm);  
    float spot_spec = pow(max(dot(spot_viewDir, spot_reflectDir), 0.0), Ns);
    vec3 spot_specular = Ks * spot_spec * spotLightIntensity;
        
    // Attenuation
    float spot_distance    = length(spotLightPosition - iPosWorld);
    float spot_attenuation = 1.0f / (spot_distance * spot_distance);    

    spot_diffuse  *= spot_attenuation*spot_intensity;
    spot_specular *= spot_attenuation*spot_intensity;   
                
    FragColor += vec4( spot_diffuse + spot_specular, 1.0f)/2;



}
